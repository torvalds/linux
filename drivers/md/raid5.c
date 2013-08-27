/*
 * raid5.c : Multiple Devices driver for Linux
 *	   Copyright (C) 1996, 1997 Ingo Molnar, Miguel de Icaza, Gadi Oxman
 *	   Copyright (C) 1999, 2000 Ingo Molnar
 *	   Copyright (C) 2002, 2003 H. Peter Anvin
 *
 * RAID-4/5/6 management functions.
 * Thanks to Penguin Computing for making the RAID-6 development possible
 * by donating a test server!
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * BITMAP UNPLUGGING:
 *
 * The sequencing for updating the bitmap reliably is a little
 * subtle (and I got it wrong the first time) so it deserves some
 * explanation.
 *
 * We group bitmap updates into batches.  Each batch has a number.
 * We may write out several batches at once, but that isn't very important.
 * conf->seq_write is the number of the last batch successfully written.
 * conf->seq_flush is the number of the last batch that was closed to
 *    new additions.
 * When we discover that we will need to write to any block in a stripe
 * (in add_stripe_bio) we update the in-memory bitmap and record in sh->bm_seq
 * the number of the batch it will be in. This is seq_flush+1.
 * When we are ready to do a write, if that batch hasn't been written yet,
 *   we plug the array and queue the stripe for later.
 * When an unplug happens, we increment bm_flush, thus closing the current
 *   batch.
 * When we notice that bm_flush > bm_write, we write out all pending updates
 * to the bitmap, and advance bm_write to where bm_flush was.
 * This may occasionally write a bit out twice, but is sure never to
 * miss any bits.
 */

#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/raid/pq.h>
#include <linux/async_tx.h>
#include <linux/module.h>
#include <linux/async.h>
#include <linux/seq_file.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include <linux/nodemask.h>
#include <trace/events/block.h>

#include "md.h"
#include "raid5.h"
#include "raid0.h"
#include "bitmap.h"

#define cpu_to_group(cpu) cpu_to_node(cpu)
#define ANY_GROUP NUMA_NO_NODE

static struct workqueue_struct *raid5_wq;
/*
 * Stripe cache
 */

#define NR_STRIPES		256
#define STRIPE_SIZE		PAGE_SIZE
#define STRIPE_SHIFT		(PAGE_SHIFT - 9)
#define STRIPE_SECTORS		(STRIPE_SIZE>>9)
#define	IO_THRESHOLD		1
#define BYPASS_THRESHOLD	1
#define NR_HASH			(PAGE_SIZE / sizeof(struct hlist_head))
#define HASH_MASK		(NR_HASH - 1)

static inline struct hlist_head *stripe_hash(struct r5conf *conf, sector_t sect)
{
	int hash = (sect >> STRIPE_SHIFT) & HASH_MASK;
	return &conf->stripe_hashtbl[hash];
}

/* bio's attached to a stripe+device for I/O are linked together in bi_sector
 * order without overlap.  There may be several bio's per stripe+device, and
 * a bio could span several devices.
 * When walking this list for a particular stripe+device, we must never proceed
 * beyond a bio that extends past this device, as the next bio might no longer
 * be valid.
 * This function is used to determine the 'next' bio in the list, given the sector
 * of the current stripe+device
 */
static inline struct bio *r5_next_bio(struct bio *bio, sector_t sector)
{
	int sectors = bio_sectors(bio);
	if (bio->bi_sector + sectors < sector + STRIPE_SECTORS)
		return bio->bi_next;
	else
		return NULL;
}

/*
 * We maintain a biased count of active stripes in the bottom 16 bits of
 * bi_phys_segments, and a count of processed stripes in the upper 16 bits
 */
static inline int raid5_bi_processed_stripes(struct bio *bio)
{
	atomic_t *segments = (atomic_t *)&bio->bi_phys_segments;
	return (atomic_read(segments) >> 16) & 0xffff;
}

static inline int raid5_dec_bi_active_stripes(struct bio *bio)
{
	atomic_t *segments = (atomic_t *)&bio->bi_phys_segments;
	return atomic_sub_return(1, segments) & 0xffff;
}

static inline void raid5_inc_bi_active_stripes(struct bio *bio)
{
	atomic_t *segments = (atomic_t *)&bio->bi_phys_segments;
	atomic_inc(segments);
}

static inline void raid5_set_bi_processed_stripes(struct bio *bio,
	unsigned int cnt)
{
	atomic_t *segments = (atomic_t *)&bio->bi_phys_segments;
	int old, new;

	do {
		old = atomic_read(segments);
		new = (old & 0xffff) | (cnt << 16);
	} while (atomic_cmpxchg(segments, old, new) != old);
}

static inline void raid5_set_bi_stripes(struct bio *bio, unsigned int cnt)
{
	atomic_t *segments = (atomic_t *)&bio->bi_phys_segments;
	atomic_set(segments, cnt);
}

/* Find first data disk in a raid6 stripe */
static inline int raid6_d0(struct stripe_head *sh)
{
	if (sh->ddf_layout)
		/* ddf always start from first device */
		return 0;
	/* md starts just after Q block */
	if (sh->qd_idx == sh->disks - 1)
		return 0;
	else
		return sh->qd_idx + 1;
}
static inline int raid6_next_disk(int disk, int raid_disks)
{
	disk++;
	return (disk < raid_disks) ? disk : 0;
}

/* When walking through the disks in a raid5, starting at raid6_d0,
 * We need to map each disk to a 'slot', where the data disks are slot
 * 0 .. raid_disks-3, the parity disk is raid_disks-2 and the Q disk
 * is raid_disks-1.  This help does that mapping.
 */
static int raid6_idx_to_slot(int idx, struct stripe_head *sh,
			     int *count, int syndrome_disks)
{
	int slot = *count;

	if (sh->ddf_layout)
		(*count)++;
	if (idx == sh->pd_idx)
		return syndrome_disks;
	if (idx == sh->qd_idx)
		return syndrome_disks + 1;
	if (!sh->ddf_layout)
		(*count)++;
	return slot;
}

static void return_io(struct bio *return_bi)
{
	struct bio *bi = return_bi;
	while (bi) {

		return_bi = bi->bi_next;
		bi->bi_next = NULL;
		bi->bi_size = 0;
		trace_block_bio_complete(bdev_get_queue(bi->bi_bdev),
					 bi, 0);
		bio_endio(bi, 0);
		bi = return_bi;
	}
}

static void print_raid5_conf (struct r5conf *conf);

static int stripe_operations_active(struct stripe_head *sh)
{
	return sh->check_state || sh->reconstruct_state ||
	       test_bit(STRIPE_BIOFILL_RUN, &sh->state) ||
	       test_bit(STRIPE_COMPUTE_RUN, &sh->state);
}

static void raid5_wakeup_stripe_thread(struct stripe_head *sh)
{
	struct r5conf *conf = sh->raid_conf;
	struct r5worker_group *group;
	int i, cpu = sh->cpu;

	if (!cpu_online(cpu)) {
		cpu = cpumask_any(cpu_online_mask);
		sh->cpu = cpu;
	}

	if (list_empty(&sh->lru)) {
		struct r5worker_group *group;
		group = conf->worker_groups + cpu_to_group(cpu);
		list_add_tail(&sh->lru, &group->handle_list);
	}

	if (conf->worker_cnt_per_group == 0) {
		md_wakeup_thread(conf->mddev->thread);
		return;
	}

	group = conf->worker_groups + cpu_to_group(sh->cpu);

	for (i = 0; i < conf->worker_cnt_per_group; i++)
		queue_work_on(sh->cpu, raid5_wq, &group->workers[i].work);
}

static void do_release_stripe(struct r5conf *conf, struct stripe_head *sh)
{
	BUG_ON(!list_empty(&sh->lru));
	BUG_ON(atomic_read(&conf->active_stripes)==0);
	if (test_bit(STRIPE_HANDLE, &sh->state)) {
		if (test_bit(STRIPE_DELAYED, &sh->state) &&
		    !test_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
			list_add_tail(&sh->lru, &conf->delayed_list);
		else if (test_bit(STRIPE_BIT_DELAY, &sh->state) &&
			   sh->bm_seq - conf->seq_write > 0)
			list_add_tail(&sh->lru, &conf->bitmap_list);
		else {
			clear_bit(STRIPE_DELAYED, &sh->state);
			clear_bit(STRIPE_BIT_DELAY, &sh->state);
			if (conf->worker_cnt_per_group == 0) {
				list_add_tail(&sh->lru, &conf->handle_list);
			} else {
				raid5_wakeup_stripe_thread(sh);
				return;
			}
		}
		md_wakeup_thread(conf->mddev->thread);
	} else {
		BUG_ON(stripe_operations_active(sh));
		if (test_and_clear_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
			if (atomic_dec_return(&conf->preread_active_stripes)
			    < IO_THRESHOLD)
				md_wakeup_thread(conf->mddev->thread);
		atomic_dec(&conf->active_stripes);
		if (!test_bit(STRIPE_EXPANDING, &sh->state)) {
			list_add_tail(&sh->lru, &conf->inactive_list);
			wake_up(&conf->wait_for_stripe);
			if (conf->retry_read_aligned)
				md_wakeup_thread(conf->mddev->thread);
		}
	}
}

static void __release_stripe(struct r5conf *conf, struct stripe_head *sh)
{
	if (atomic_dec_and_test(&sh->count))
		do_release_stripe(conf, sh);
}

static struct llist_node *llist_reverse_order(struct llist_node *head)
{
	struct llist_node *new_head = NULL;

	while (head) {
		struct llist_node *tmp = head;
		head = head->next;
		tmp->next = new_head;
		new_head = tmp;
	}

	return new_head;
}

/* should hold conf->device_lock already */
static int release_stripe_list(struct r5conf *conf)
{
	struct stripe_head *sh;
	int count = 0;
	struct llist_node *head;

	head = llist_del_all(&conf->released_stripes);
	head = llist_reverse_order(head);
	while (head) {
		sh = llist_entry(head, struct stripe_head, release_list);
		head = llist_next(head);
		/* sh could be readded after STRIPE_ON_RELEASE_LIST is cleard */
		smp_mb();
		clear_bit(STRIPE_ON_RELEASE_LIST, &sh->state);
		/*
		 * Don't worry the bit is set here, because if the bit is set
		 * again, the count is always > 1. This is true for
		 * STRIPE_ON_UNPLUG_LIST bit too.
		 */
		__release_stripe(conf, sh);
		count++;
	}

	return count;
}

static void release_stripe(struct stripe_head *sh)
{
	struct r5conf *conf = sh->raid_conf;
	unsigned long flags;
	bool wakeup;

	if (test_and_set_bit(STRIPE_ON_RELEASE_LIST, &sh->state))
		goto slow_path;
	wakeup = llist_add(&sh->release_list, &conf->released_stripes);
	if (wakeup)
		md_wakeup_thread(conf->mddev->thread);
	return;
slow_path:
	local_irq_save(flags);
	/* we are ok here if STRIPE_ON_RELEASE_LIST is set or not */
	if (atomic_dec_and_lock(&sh->count, &conf->device_lock)) {
		do_release_stripe(conf, sh);
		spin_unlock(&conf->device_lock);
	}
	local_irq_restore(flags);
}

static inline void remove_hash(struct stripe_head *sh)
{
	pr_debug("remove_hash(), stripe %llu\n",
		(unsigned long long)sh->sector);

	hlist_del_init(&sh->hash);
}

static inline void insert_hash(struct r5conf *conf, struct stripe_head *sh)
{
	struct hlist_head *hp = stripe_hash(conf, sh->sector);

	pr_debug("insert_hash(), stripe %llu\n",
		(unsigned long long)sh->sector);

	hlist_add_head(&sh->hash, hp);
}


/* find an idle stripe, make sure it is unhashed, and return it. */
static struct stripe_head *get_free_stripe(struct r5conf *conf)
{
	struct stripe_head *sh = NULL;
	struct list_head *first;

	if (list_empty(&conf->inactive_list))
		goto out;
	first = conf->inactive_list.next;
	sh = list_entry(first, struct stripe_head, lru);
	list_del_init(first);
	remove_hash(sh);
	atomic_inc(&conf->active_stripes);
out:
	return sh;
}

static void shrink_buffers(struct stripe_head *sh)
{
	struct page *p;
	int i;
	int num = sh->raid_conf->pool_size;

	for (i = 0; i < num ; i++) {
		p = sh->dev[i].page;
		if (!p)
			continue;
		sh->dev[i].page = NULL;
		put_page(p);
	}
}

static int grow_buffers(struct stripe_head *sh)
{
	int i;
	int num = sh->raid_conf->pool_size;

	for (i = 0; i < num; i++) {
		struct page *page;

		if (!(page = alloc_page(GFP_KERNEL))) {
			return 1;
		}
		sh->dev[i].page = page;
	}
	return 0;
}

static void raid5_build_block(struct stripe_head *sh, int i, int previous);
static void stripe_set_idx(sector_t stripe, struct r5conf *conf, int previous,
			    struct stripe_head *sh);

static void init_stripe(struct stripe_head *sh, sector_t sector, int previous)
{
	struct r5conf *conf = sh->raid_conf;
	int i;

	BUG_ON(atomic_read(&sh->count) != 0);
	BUG_ON(test_bit(STRIPE_HANDLE, &sh->state));
	BUG_ON(stripe_operations_active(sh));

	pr_debug("init_stripe called, stripe %llu\n",
		(unsigned long long)sh->sector);

	remove_hash(sh);

	sh->generation = conf->generation - previous;
	sh->disks = previous ? conf->previous_raid_disks : conf->raid_disks;
	sh->sector = sector;
	stripe_set_idx(sector, conf, previous, sh);
	sh->state = 0;


	for (i = sh->disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];

		if (dev->toread || dev->read || dev->towrite || dev->written ||
		    test_bit(R5_LOCKED, &dev->flags)) {
			printk(KERN_ERR "sector=%llx i=%d %p %p %p %p %d\n",
			       (unsigned long long)sh->sector, i, dev->toread,
			       dev->read, dev->towrite, dev->written,
			       test_bit(R5_LOCKED, &dev->flags));
			WARN_ON(1);
		}
		dev->flags = 0;
		raid5_build_block(sh, i, previous);
	}
	insert_hash(conf, sh);
	sh->cpu = smp_processor_id();
}

static struct stripe_head *__find_stripe(struct r5conf *conf, sector_t sector,
					 short generation)
{
	struct stripe_head *sh;

	pr_debug("__find_stripe, sector %llu\n", (unsigned long long)sector);
	hlist_for_each_entry(sh, stripe_hash(conf, sector), hash)
		if (sh->sector == sector && sh->generation == generation)
			return sh;
	pr_debug("__stripe %llu not in cache\n", (unsigned long long)sector);
	return NULL;
}

/*
 * Need to check if array has failed when deciding whether to:
 *  - start an array
 *  - remove non-faulty devices
 *  - add a spare
 *  - allow a reshape
 * This determination is simple when no reshape is happening.
 * However if there is a reshape, we need to carefully check
 * both the before and after sections.
 * This is because some failed devices may only affect one
 * of the two sections, and some non-in_sync devices may
 * be insync in the section most affected by failed devices.
 */
static int calc_degraded(struct r5conf *conf)
{
	int degraded, degraded2;
	int i;

	rcu_read_lock();
	degraded = 0;
	for (i = 0; i < conf->previous_raid_disks; i++) {
		struct md_rdev *rdev = rcu_dereference(conf->disks[i].rdev);
		if (rdev && test_bit(Faulty, &rdev->flags))
			rdev = rcu_dereference(conf->disks[i].replacement);
		if (!rdev || test_bit(Faulty, &rdev->flags))
			degraded++;
		else if (test_bit(In_sync, &rdev->flags))
			;
		else
			/* not in-sync or faulty.
			 * If the reshape increases the number of devices,
			 * this is being recovered by the reshape, so
			 * this 'previous' section is not in_sync.
			 * If the number of devices is being reduced however,
			 * the device can only be part of the array if
			 * we are reverting a reshape, so this section will
			 * be in-sync.
			 */
			if (conf->raid_disks >= conf->previous_raid_disks)
				degraded++;
	}
	rcu_read_unlock();
	if (conf->raid_disks == conf->previous_raid_disks)
		return degraded;
	rcu_read_lock();
	degraded2 = 0;
	for (i = 0; i < conf->raid_disks; i++) {
		struct md_rdev *rdev = rcu_dereference(conf->disks[i].rdev);
		if (rdev && test_bit(Faulty, &rdev->flags))
			rdev = rcu_dereference(conf->disks[i].replacement);
		if (!rdev || test_bit(Faulty, &rdev->flags))
			degraded2++;
		else if (test_bit(In_sync, &rdev->flags))
			;
		else
			/* not in-sync or faulty.
			 * If reshape increases the number of devices, this
			 * section has already been recovered, else it
			 * almost certainly hasn't.
			 */
			if (conf->raid_disks <= conf->previous_raid_disks)
				degraded2++;
	}
	rcu_read_unlock();
	if (degraded2 > degraded)
		return degraded2;
	return degraded;
}

static int has_failed(struct r5conf *conf)
{
	int degraded;

	if (conf->mddev->reshape_position == MaxSector)
		return conf->mddev->degraded > conf->max_degraded;

	degraded = calc_degraded(conf);
	if (degraded > conf->max_degraded)
		return 1;
	return 0;
}

static struct stripe_head *
get_active_stripe(struct r5conf *conf, sector_t sector,
		  int previous, int noblock, int noquiesce)
{
	struct stripe_head *sh;

	pr_debug("get_stripe, sector %llu\n", (unsigned long long)sector);

	spin_lock_irq(&conf->device_lock);

	do {
		wait_event_lock_irq(conf->wait_for_stripe,
				    conf->quiesce == 0 || noquiesce,
				    conf->device_lock);
		sh = __find_stripe(conf, sector, conf->generation - previous);
		if (!sh) {
			if (!conf->inactive_blocked)
				sh = get_free_stripe(conf);
			if (noblock && sh == NULL)
				break;
			if (!sh) {
				conf->inactive_blocked = 1;
				wait_event_lock_irq(conf->wait_for_stripe,
						    !list_empty(&conf->inactive_list) &&
						    (atomic_read(&conf->active_stripes)
						     < (conf->max_nr_stripes *3/4)
						     || !conf->inactive_blocked),
						    conf->device_lock);
				conf->inactive_blocked = 0;
			} else
				init_stripe(sh, sector, previous);
		} else {
			if (atomic_read(&sh->count)) {
				BUG_ON(!list_empty(&sh->lru)
				    && !test_bit(STRIPE_EXPANDING, &sh->state)
				    && !test_bit(STRIPE_ON_UNPLUG_LIST, &sh->state)
				    && !test_bit(STRIPE_ON_RELEASE_LIST, &sh->state));
			} else {
				if (!test_bit(STRIPE_HANDLE, &sh->state))
					atomic_inc(&conf->active_stripes);
				if (list_empty(&sh->lru) &&
				    !test_bit(STRIPE_EXPANDING, &sh->state))
					BUG();
				list_del_init(&sh->lru);
			}
		}
	} while (sh == NULL);

	if (sh)
		atomic_inc(&sh->count);

	spin_unlock_irq(&conf->device_lock);
	return sh;
}

/* Determine if 'data_offset' or 'new_data_offset' should be used
 * in this stripe_head.
 */
static int use_new_offset(struct r5conf *conf, struct stripe_head *sh)
{
	sector_t progress = conf->reshape_progress;
	/* Need a memory barrier to make sure we see the value
	 * of conf->generation, or ->data_offset that was set before
	 * reshape_progress was updated.
	 */
	smp_rmb();
	if (progress == MaxSector)
		return 0;
	if (sh->generation == conf->generation - 1)
		return 0;
	/* We are in a reshape, and this is a new-generation stripe,
	 * so use new_data_offset.
	 */
	return 1;
}

static void
raid5_end_read_request(struct bio *bi, int error);
static void
raid5_end_write_request(struct bio *bi, int error);

static void ops_run_io(struct stripe_head *sh, struct stripe_head_state *s)
{
	struct r5conf *conf = sh->raid_conf;
	int i, disks = sh->disks;

	might_sleep();

	for (i = disks; i--; ) {
		int rw;
		int replace_only = 0;
		struct bio *bi, *rbi;
		struct md_rdev *rdev, *rrdev = NULL;
		if (test_and_clear_bit(R5_Wantwrite, &sh->dev[i].flags)) {
			if (test_and_clear_bit(R5_WantFUA, &sh->dev[i].flags))
				rw = WRITE_FUA;
			else
				rw = WRITE;
			if (test_bit(R5_Discard, &sh->dev[i].flags))
				rw |= REQ_DISCARD;
		} else if (test_and_clear_bit(R5_Wantread, &sh->dev[i].flags))
			rw = READ;
		else if (test_and_clear_bit(R5_WantReplace,
					    &sh->dev[i].flags)) {
			rw = WRITE;
			replace_only = 1;
		} else
			continue;
		if (test_and_clear_bit(R5_SyncIO, &sh->dev[i].flags))
			rw |= REQ_SYNC;

		bi = &sh->dev[i].req;
		rbi = &sh->dev[i].rreq; /* For writing to replacement */

		rcu_read_lock();
		rrdev = rcu_dereference(conf->disks[i].replacement);
		smp_mb(); /* Ensure that if rrdev is NULL, rdev won't be */
		rdev = rcu_dereference(conf->disks[i].rdev);
		if (!rdev) {
			rdev = rrdev;
			rrdev = NULL;
		}
		if (rw & WRITE) {
			if (replace_only)
				rdev = NULL;
			if (rdev == rrdev)
				/* We raced and saw duplicates */
				rrdev = NULL;
		} else {
			if (test_bit(R5_ReadRepl, &sh->dev[i].flags) && rrdev)
				rdev = rrdev;
			rrdev = NULL;
		}

		if (rdev && test_bit(Faulty, &rdev->flags))
			rdev = NULL;
		if (rdev)
			atomic_inc(&rdev->nr_pending);
		if (rrdev && test_bit(Faulty, &rrdev->flags))
			rrdev = NULL;
		if (rrdev)
			atomic_inc(&rrdev->nr_pending);
		rcu_read_unlock();

		/* We have already checked bad blocks for reads.  Now
		 * need to check for writes.  We never accept write errors
		 * on the replacement, so we don't to check rrdev.
		 */
		while ((rw & WRITE) && rdev &&
		       test_bit(WriteErrorSeen, &rdev->flags)) {
			sector_t first_bad;
			int bad_sectors;
			int bad = is_badblock(rdev, sh->sector, STRIPE_SECTORS,
					      &first_bad, &bad_sectors);
			if (!bad)
				break;

			if (bad < 0) {
				set_bit(BlockedBadBlocks, &rdev->flags);
				if (!conf->mddev->external &&
				    conf->mddev->flags) {
					/* It is very unlikely, but we might
					 * still need to write out the
					 * bad block log - better give it
					 * a chance*/
					md_check_recovery(conf->mddev);
				}
				/*
				 * Because md_wait_for_blocked_rdev
				 * will dec nr_pending, we must
				 * increment it first.
				 */
				atomic_inc(&rdev->nr_pending);
				md_wait_for_blocked_rdev(rdev, conf->mddev);
			} else {
				/* Acknowledged bad block - skip the write */
				rdev_dec_pending(rdev, conf->mddev);
				rdev = NULL;
			}
		}

		if (rdev) {
			if (s->syncing || s->expanding || s->expanded
			    || s->replacing)
				md_sync_acct(rdev->bdev, STRIPE_SECTORS);

			set_bit(STRIPE_IO_STARTED, &sh->state);

			bio_reset(bi);
			bi->bi_bdev = rdev->bdev;
			bi->bi_rw = rw;
			bi->bi_end_io = (rw & WRITE)
				? raid5_end_write_request
				: raid5_end_read_request;
			bi->bi_private = sh;

			pr_debug("%s: for %llu schedule op %ld on disc %d\n",
				__func__, (unsigned long long)sh->sector,
				bi->bi_rw, i);
			atomic_inc(&sh->count);
			if (use_new_offset(conf, sh))
				bi->bi_sector = (sh->sector
						 + rdev->new_data_offset);
			else
				bi->bi_sector = (sh->sector
						 + rdev->data_offset);
			if (test_bit(R5_ReadNoMerge, &sh->dev[i].flags))
				bi->bi_rw |= REQ_FLUSH;

			bi->bi_vcnt = 1;
			bi->bi_io_vec[0].bv_len = STRIPE_SIZE;
			bi->bi_io_vec[0].bv_offset = 0;
			bi->bi_size = STRIPE_SIZE;
			if (rrdev)
				set_bit(R5_DOUBLE_LOCKED, &sh->dev[i].flags);

			if (conf->mddev->gendisk)
				trace_block_bio_remap(bdev_get_queue(bi->bi_bdev),
						      bi, disk_devt(conf->mddev->gendisk),
						      sh->dev[i].sector);
			generic_make_request(bi);
		}
		if (rrdev) {
			if (s->syncing || s->expanding || s->expanded
			    || s->replacing)
				md_sync_acct(rrdev->bdev, STRIPE_SECTORS);

			set_bit(STRIPE_IO_STARTED, &sh->state);

			bio_reset(rbi);
			rbi->bi_bdev = rrdev->bdev;
			rbi->bi_rw = rw;
			BUG_ON(!(rw & WRITE));
			rbi->bi_end_io = raid5_end_write_request;
			rbi->bi_private = sh;

			pr_debug("%s: for %llu schedule op %ld on "
				 "replacement disc %d\n",
				__func__, (unsigned long long)sh->sector,
				rbi->bi_rw, i);
			atomic_inc(&sh->count);
			if (use_new_offset(conf, sh))
				rbi->bi_sector = (sh->sector
						  + rrdev->new_data_offset);
			else
				rbi->bi_sector = (sh->sector
						  + rrdev->data_offset);
			rbi->bi_vcnt = 1;
			rbi->bi_io_vec[0].bv_len = STRIPE_SIZE;
			rbi->bi_io_vec[0].bv_offset = 0;
			rbi->bi_size = STRIPE_SIZE;
			if (conf->mddev->gendisk)
				trace_block_bio_remap(bdev_get_queue(rbi->bi_bdev),
						      rbi, disk_devt(conf->mddev->gendisk),
						      sh->dev[i].sector);
			generic_make_request(rbi);
		}
		if (!rdev && !rrdev) {
			if (rw & WRITE)
				set_bit(STRIPE_DEGRADED, &sh->state);
			pr_debug("skip op %ld on disc %d for sector %llu\n",
				bi->bi_rw, i, (unsigned long long)sh->sector);
			clear_bit(R5_LOCKED, &sh->dev[i].flags);
			set_bit(STRIPE_HANDLE, &sh->state);
		}
	}
}

static struct dma_async_tx_descriptor *
async_copy_data(int frombio, struct bio *bio, struct page *page,
	sector_t sector, struct dma_async_tx_descriptor *tx)
{
	struct bio_vec *bvl;
	struct page *bio_page;
	int i;
	int page_offset;
	struct async_submit_ctl submit;
	enum async_tx_flags flags = 0;

	if (bio->bi_sector >= sector)
		page_offset = (signed)(bio->bi_sector - sector) * 512;
	else
		page_offset = (signed)(sector - bio->bi_sector) * -512;

	if (frombio)
		flags |= ASYNC_TX_FENCE;
	init_async_submit(&submit, flags, tx, NULL, NULL, NULL);

	bio_for_each_segment(bvl, bio, i) {
		int len = bvl->bv_len;
		int clen;
		int b_offset = 0;

		if (page_offset < 0) {
			b_offset = -page_offset;
			page_offset += b_offset;
			len -= b_offset;
		}

		if (len > 0 && page_offset + len > STRIPE_SIZE)
			clen = STRIPE_SIZE - page_offset;
		else
			clen = len;

		if (clen > 0) {
			b_offset += bvl->bv_offset;
			bio_page = bvl->bv_page;
			if (frombio)
				tx = async_memcpy(page, bio_page, page_offset,
						  b_offset, clen, &submit);
			else
				tx = async_memcpy(bio_page, page, b_offset,
						  page_offset, clen, &submit);
		}
		/* chain the operations */
		submit.depend_tx = tx;

		if (clen < len) /* hit end of page */
			break;
		page_offset +=  len;
	}

	return tx;
}

static void ops_complete_biofill(void *stripe_head_ref)
{
	struct stripe_head *sh = stripe_head_ref;
	struct bio *return_bi = NULL;
	int i;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	/* clear completed biofills */
	for (i = sh->disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];

		/* acknowledge completion of a biofill operation */
		/* and check if we need to reply to a read request,
		 * new R5_Wantfill requests are held off until
		 * !STRIPE_BIOFILL_RUN
		 */
		if (test_and_clear_bit(R5_Wantfill, &dev->flags)) {
			struct bio *rbi, *rbi2;

			BUG_ON(!dev->read);
			rbi = dev->read;
			dev->read = NULL;
			while (rbi && rbi->bi_sector <
				dev->sector + STRIPE_SECTORS) {
				rbi2 = r5_next_bio(rbi, dev->sector);
				if (!raid5_dec_bi_active_stripes(rbi)) {
					rbi->bi_next = return_bi;
					return_bi = rbi;
				}
				rbi = rbi2;
			}
		}
	}
	clear_bit(STRIPE_BIOFILL_RUN, &sh->state);

	return_io(return_bi);

	set_bit(STRIPE_HANDLE, &sh->state);
	release_stripe(sh);
}

static void ops_run_biofill(struct stripe_head *sh)
{
	struct dma_async_tx_descriptor *tx = NULL;
	struct async_submit_ctl submit;
	int i;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	for (i = sh->disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];
		if (test_bit(R5_Wantfill, &dev->flags)) {
			struct bio *rbi;
			spin_lock_irq(&sh->stripe_lock);
			dev->read = rbi = dev->toread;
			dev->toread = NULL;
			spin_unlock_irq(&sh->stripe_lock);
			while (rbi && rbi->bi_sector <
				dev->sector + STRIPE_SECTORS) {
				tx = async_copy_data(0, rbi, dev->page,
					dev->sector, tx);
				rbi = r5_next_bio(rbi, dev->sector);
			}
		}
	}

	atomic_inc(&sh->count);
	init_async_submit(&submit, ASYNC_TX_ACK, tx, ops_complete_biofill, sh, NULL);
	async_trigger_callback(&submit);
}

static void mark_target_uptodate(struct stripe_head *sh, int target)
{
	struct r5dev *tgt;

	if (target < 0)
		return;

	tgt = &sh->dev[target];
	set_bit(R5_UPTODATE, &tgt->flags);
	BUG_ON(!test_bit(R5_Wantcompute, &tgt->flags));
	clear_bit(R5_Wantcompute, &tgt->flags);
}

static void ops_complete_compute(void *stripe_head_ref)
{
	struct stripe_head *sh = stripe_head_ref;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	/* mark the computed target(s) as uptodate */
	mark_target_uptodate(sh, sh->ops.target);
	mark_target_uptodate(sh, sh->ops.target2);

	clear_bit(STRIPE_COMPUTE_RUN, &sh->state);
	if (sh->check_state == check_state_compute_run)
		sh->check_state = check_state_compute_result;
	set_bit(STRIPE_HANDLE, &sh->state);
	release_stripe(sh);
}

/* return a pointer to the address conversion region of the scribble buffer */
static addr_conv_t *to_addr_conv(struct stripe_head *sh,
				 struct raid5_percpu *percpu)
{
	return percpu->scribble + sizeof(struct page *) * (sh->disks + 2);
}

static struct dma_async_tx_descriptor *
ops_run_compute5(struct stripe_head *sh, struct raid5_percpu *percpu)
{
	int disks = sh->disks;
	struct page **xor_srcs = percpu->scribble;
	int target = sh->ops.target;
	struct r5dev *tgt = &sh->dev[target];
	struct page *xor_dest = tgt->page;
	int count = 0;
	struct dma_async_tx_descriptor *tx;
	struct async_submit_ctl submit;
	int i;

	pr_debug("%s: stripe %llu block: %d\n",
		__func__, (unsigned long long)sh->sector, target);
	BUG_ON(!test_bit(R5_Wantcompute, &tgt->flags));

	for (i = disks; i--; )
		if (i != target)
			xor_srcs[count++] = sh->dev[i].page;

	atomic_inc(&sh->count);

	init_async_submit(&submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_ZERO_DST, NULL,
			  ops_complete_compute, sh, to_addr_conv(sh, percpu));
	if (unlikely(count == 1))
		tx = async_memcpy(xor_dest, xor_srcs[0], 0, 0, STRIPE_SIZE, &submit);
	else
		tx = async_xor(xor_dest, xor_srcs, 0, count, STRIPE_SIZE, &submit);

	return tx;
}

/* set_syndrome_sources - populate source buffers for gen_syndrome
 * @srcs - (struct page *) array of size sh->disks
 * @sh - stripe_head to parse
 *
 * Populates srcs in proper layout order for the stripe and returns the
 * 'count' of sources to be used in a call to async_gen_syndrome.  The P
 * destination buffer is recorded in srcs[count] and the Q destination
 * is recorded in srcs[count+1]].
 */
static int set_syndrome_sources(struct page **srcs, struct stripe_head *sh)
{
	int disks = sh->disks;
	int syndrome_disks = sh->ddf_layout ? disks : (disks - 2);
	int d0_idx = raid6_d0(sh);
	int count;
	int i;

	for (i = 0; i < disks; i++)
		srcs[i] = NULL;

	count = 0;
	i = d0_idx;
	do {
		int slot = raid6_idx_to_slot(i, sh, &count, syndrome_disks);

		srcs[slot] = sh->dev[i].page;
		i = raid6_next_disk(i, disks);
	} while (i != d0_idx);

	return syndrome_disks;
}

static struct dma_async_tx_descriptor *
ops_run_compute6_1(struct stripe_head *sh, struct raid5_percpu *percpu)
{
	int disks = sh->disks;
	struct page **blocks = percpu->scribble;
	int target;
	int qd_idx = sh->qd_idx;
	struct dma_async_tx_descriptor *tx;
	struct async_submit_ctl submit;
	struct r5dev *tgt;
	struct page *dest;
	int i;
	int count;

	if (sh->ops.target < 0)
		target = sh->ops.target2;
	else if (sh->ops.target2 < 0)
		target = sh->ops.target;
	else
		/* we should only have one valid target */
		BUG();
	BUG_ON(target < 0);
	pr_debug("%s: stripe %llu block: %d\n",
		__func__, (unsigned long long)sh->sector, target);

	tgt = &sh->dev[target];
	BUG_ON(!test_bit(R5_Wantcompute, &tgt->flags));
	dest = tgt->page;

	atomic_inc(&sh->count);

	if (target == qd_idx) {
		count = set_syndrome_sources(blocks, sh);
		blocks[count] = NULL; /* regenerating p is not necessary */
		BUG_ON(blocks[count+1] != dest); /* q should already be set */
		init_async_submit(&submit, ASYNC_TX_FENCE, NULL,
				  ops_complete_compute, sh,
				  to_addr_conv(sh, percpu));
		tx = async_gen_syndrome(blocks, 0, count+2, STRIPE_SIZE, &submit);
	} else {
		/* Compute any data- or p-drive using XOR */
		count = 0;
		for (i = disks; i-- ; ) {
			if (i == target || i == qd_idx)
				continue;
			blocks[count++] = sh->dev[i].page;
		}

		init_async_submit(&submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_ZERO_DST,
				  NULL, ops_complete_compute, sh,
				  to_addr_conv(sh, percpu));
		tx = async_xor(dest, blocks, 0, count, STRIPE_SIZE, &submit);
	}

	return tx;
}

static struct dma_async_tx_descriptor *
ops_run_compute6_2(struct stripe_head *sh, struct raid5_percpu *percpu)
{
	int i, count, disks = sh->disks;
	int syndrome_disks = sh->ddf_layout ? disks : disks-2;
	int d0_idx = raid6_d0(sh);
	int faila = -1, failb = -1;
	int target = sh->ops.target;
	int target2 = sh->ops.target2;
	struct r5dev *tgt = &sh->dev[target];
	struct r5dev *tgt2 = &sh->dev[target2];
	struct dma_async_tx_descriptor *tx;
	struct page **blocks = percpu->scribble;
	struct async_submit_ctl submit;

	pr_debug("%s: stripe %llu block1: %d block2: %d\n",
		 __func__, (unsigned long long)sh->sector, target, target2);
	BUG_ON(target < 0 || target2 < 0);
	BUG_ON(!test_bit(R5_Wantcompute, &tgt->flags));
	BUG_ON(!test_bit(R5_Wantcompute, &tgt2->flags));

	/* we need to open-code set_syndrome_sources to handle the
	 * slot number conversion for 'faila' and 'failb'
	 */
	for (i = 0; i < disks ; i++)
		blocks[i] = NULL;
	count = 0;
	i = d0_idx;
	do {
		int slot = raid6_idx_to_slot(i, sh, &count, syndrome_disks);

		blocks[slot] = sh->dev[i].page;

		if (i == target)
			faila = slot;
		if (i == target2)
			failb = slot;
		i = raid6_next_disk(i, disks);
	} while (i != d0_idx);

	BUG_ON(faila == failb);
	if (failb < faila)
		swap(faila, failb);
	pr_debug("%s: stripe: %llu faila: %d failb: %d\n",
		 __func__, (unsigned long long)sh->sector, faila, failb);

	atomic_inc(&sh->count);

	if (failb == syndrome_disks+1) {
		/* Q disk is one of the missing disks */
		if (faila == syndrome_disks) {
			/* Missing P+Q, just recompute */
			init_async_submit(&submit, ASYNC_TX_FENCE, NULL,
					  ops_complete_compute, sh,
					  to_addr_conv(sh, percpu));
			return async_gen_syndrome(blocks, 0, syndrome_disks+2,
						  STRIPE_SIZE, &submit);
		} else {
			struct page *dest;
			int data_target;
			int qd_idx = sh->qd_idx;

			/* Missing D+Q: recompute D from P, then recompute Q */
			if (target == qd_idx)
				data_target = target2;
			else
				data_target = target;

			count = 0;
			for (i = disks; i-- ; ) {
				if (i == data_target || i == qd_idx)
					continue;
				blocks[count++] = sh->dev[i].page;
			}
			dest = sh->dev[data_target].page;
			init_async_submit(&submit,
					  ASYNC_TX_FENCE|ASYNC_TX_XOR_ZERO_DST,
					  NULL, NULL, NULL,
					  to_addr_conv(sh, percpu));
			tx = async_xor(dest, blocks, 0, count, STRIPE_SIZE,
				       &submit);

			count = set_syndrome_sources(blocks, sh);
			init_async_submit(&submit, ASYNC_TX_FENCE, tx,
					  ops_complete_compute, sh,
					  to_addr_conv(sh, percpu));
			return async_gen_syndrome(blocks, 0, count+2,
						  STRIPE_SIZE, &submit);
		}
	} else {
		init_async_submit(&submit, ASYNC_TX_FENCE, NULL,
				  ops_complete_compute, sh,
				  to_addr_conv(sh, percpu));
		if (failb == syndrome_disks) {
			/* We're missing D+P. */
			return async_raid6_datap_recov(syndrome_disks+2,
						       STRIPE_SIZE, faila,
						       blocks, &submit);
		} else {
			/* We're missing D+D. */
			return async_raid6_2data_recov(syndrome_disks+2,
						       STRIPE_SIZE, faila, failb,
						       blocks, &submit);
		}
	}
}


static void ops_complete_prexor(void *stripe_head_ref)
{
	struct stripe_head *sh = stripe_head_ref;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);
}

static struct dma_async_tx_descriptor *
ops_run_prexor(struct stripe_head *sh, struct raid5_percpu *percpu,
	       struct dma_async_tx_descriptor *tx)
{
	int disks = sh->disks;
	struct page **xor_srcs = percpu->scribble;
	int count = 0, pd_idx = sh->pd_idx, i;
	struct async_submit_ctl submit;

	/* existing parity data subtracted */
	struct page *xor_dest = xor_srcs[count++] = sh->dev[pd_idx].page;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	for (i = disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];
		/* Only process blocks that are known to be uptodate */
		if (test_bit(R5_Wantdrain, &dev->flags))
			xor_srcs[count++] = dev->page;
	}

	init_async_submit(&submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_DROP_DST, tx,
			  ops_complete_prexor, sh, to_addr_conv(sh, percpu));
	tx = async_xor(xor_dest, xor_srcs, 0, count, STRIPE_SIZE, &submit);

	return tx;
}

static struct dma_async_tx_descriptor *
ops_run_biodrain(struct stripe_head *sh, struct dma_async_tx_descriptor *tx)
{
	int disks = sh->disks;
	int i;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	for (i = disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];
		struct bio *chosen;

		if (test_and_clear_bit(R5_Wantdrain, &dev->flags)) {
			struct bio *wbi;

			spin_lock_irq(&sh->stripe_lock);
			chosen = dev->towrite;
			dev->towrite = NULL;
			BUG_ON(dev->written);
			wbi = dev->written = chosen;
			spin_unlock_irq(&sh->stripe_lock);

			while (wbi && wbi->bi_sector <
				dev->sector + STRIPE_SECTORS) {
				if (wbi->bi_rw & REQ_FUA)
					set_bit(R5_WantFUA, &dev->flags);
				if (wbi->bi_rw & REQ_SYNC)
					set_bit(R5_SyncIO, &dev->flags);
				if (wbi->bi_rw & REQ_DISCARD)
					set_bit(R5_Discard, &dev->flags);
				else
					tx = async_copy_data(1, wbi, dev->page,
						dev->sector, tx);
				wbi = r5_next_bio(wbi, dev->sector);
			}
		}
	}

	return tx;
}

static void ops_complete_reconstruct(void *stripe_head_ref)
{
	struct stripe_head *sh = stripe_head_ref;
	int disks = sh->disks;
	int pd_idx = sh->pd_idx;
	int qd_idx = sh->qd_idx;
	int i;
	bool fua = false, sync = false, discard = false;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	for (i = disks; i--; ) {
		fua |= test_bit(R5_WantFUA, &sh->dev[i].flags);
		sync |= test_bit(R5_SyncIO, &sh->dev[i].flags);
		discard |= test_bit(R5_Discard, &sh->dev[i].flags);
	}

	for (i = disks; i--; ) {
		struct r5dev *dev = &sh->dev[i];

		if (dev->written || i == pd_idx || i == qd_idx) {
			if (!discard)
				set_bit(R5_UPTODATE, &dev->flags);
			if (fua)
				set_bit(R5_WantFUA, &dev->flags);
			if (sync)
				set_bit(R5_SyncIO, &dev->flags);
		}
	}

	if (sh->reconstruct_state == reconstruct_state_drain_run)
		sh->reconstruct_state = reconstruct_state_drain_result;
	else if (sh->reconstruct_state == reconstruct_state_prexor_drain_run)
		sh->reconstruct_state = reconstruct_state_prexor_drain_result;
	else {
		BUG_ON(sh->reconstruct_state != reconstruct_state_run);
		sh->reconstruct_state = reconstruct_state_result;
	}

	set_bit(STRIPE_HANDLE, &sh->state);
	release_stripe(sh);
}

static void
ops_run_reconstruct5(struct stripe_head *sh, struct raid5_percpu *percpu,
		     struct dma_async_tx_descriptor *tx)
{
	int disks = sh->disks;
	struct page **xor_srcs = percpu->scribble;
	struct async_submit_ctl submit;
	int count = 0, pd_idx = sh->pd_idx, i;
	struct page *xor_dest;
	int prexor = 0;
	unsigned long flags;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	for (i = 0; i < sh->disks; i++) {
		if (pd_idx == i)
			continue;
		if (!test_bit(R5_Discard, &sh->dev[i].flags))
			break;
	}
	if (i >= sh->disks) {
		atomic_inc(&sh->count);
		set_bit(R5_Discard, &sh->dev[pd_idx].flags);
		ops_complete_reconstruct(sh);
		return;
	}
	/* check if prexor is active which means only process blocks
	 * that are part of a read-modify-write (written)
	 */
	if (sh->reconstruct_state == reconstruct_state_prexor_drain_run) {
		prexor = 1;
		xor_dest = xor_srcs[count++] = sh->dev[pd_idx].page;
		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if (dev->written)
				xor_srcs[count++] = dev->page;
		}
	} else {
		xor_dest = sh->dev[pd_idx].page;
		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if (i != pd_idx)
				xor_srcs[count++] = dev->page;
		}
	}

	/* 1/ if we prexor'd then the dest is reused as a source
	 * 2/ if we did not prexor then we are redoing the parity
	 * set ASYNC_TX_XOR_DROP_DST and ASYNC_TX_XOR_ZERO_DST
	 * for the synchronous xor case
	 */
	flags = ASYNC_TX_ACK |
		(prexor ? ASYNC_TX_XOR_DROP_DST : ASYNC_TX_XOR_ZERO_DST);

	atomic_inc(&sh->count);

	init_async_submit(&submit, flags, tx, ops_complete_reconstruct, sh,
			  to_addr_conv(sh, percpu));
	if (unlikely(count == 1))
		tx = async_memcpy(xor_dest, xor_srcs[0], 0, 0, STRIPE_SIZE, &submit);
	else
		tx = async_xor(xor_dest, xor_srcs, 0, count, STRIPE_SIZE, &submit);
}

static void
ops_run_reconstruct6(struct stripe_head *sh, struct raid5_percpu *percpu,
		     struct dma_async_tx_descriptor *tx)
{
	struct async_submit_ctl submit;
	struct page **blocks = percpu->scribble;
	int count, i;

	pr_debug("%s: stripe %llu\n", __func__, (unsigned long long)sh->sector);

	for (i = 0; i < sh->disks; i++) {
		if (sh->pd_idx == i || sh->qd_idx == i)
			continue;
		if (!test_bit(R5_Discard, &sh->dev[i].flags))
			break;
	}
	if (i >= sh->disks) {
		atomic_inc(&sh->count);
		set_bit(R5_Discard, &sh->dev[sh->pd_idx].flags);
		set_bit(R5_Discard, &sh->dev[sh->qd_idx].flags);
		ops_complete_reconstruct(sh);
		return;
	}

	count = set_syndrome_sources(blocks, sh);

	atomic_inc(&sh->count);

	init_async_submit(&submit, ASYNC_TX_ACK, tx, ops_complete_reconstruct,
			  sh, to_addr_conv(sh, percpu));
	async_gen_syndrome(blocks, 0, count+2, STRIPE_SIZE,  &submit);
}

static void ops_complete_check(void *stripe_head_ref)
{
	struct stripe_head *sh = stripe_head_ref;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	sh->check_state = check_state_check_result;
	set_bit(STRIPE_HANDLE, &sh->state);
	release_stripe(sh);
}

static void ops_run_check_p(struct stripe_head *sh, struct raid5_percpu *percpu)
{
	int disks = sh->disks;
	int pd_idx = sh->pd_idx;
	int qd_idx = sh->qd_idx;
	struct page *xor_dest;
	struct page **xor_srcs = percpu->scribble;
	struct dma_async_tx_descriptor *tx;
	struct async_submit_ctl submit;
	int count;
	int i;

	pr_debug("%s: stripe %llu\n", __func__,
		(unsigned long long)sh->sector);

	count = 0;
	xor_dest = sh->dev[pd_idx].page;
	xor_srcs[count++] = xor_dest;
	for (i = disks; i--; ) {
		if (i == pd_idx || i == qd_idx)
			continue;
		xor_srcs[count++] = sh->dev[i].page;
	}

	init_async_submit(&submit, 0, NULL, NULL, NULL,
			  to_addr_conv(sh, percpu));
	tx = async_xor_val(xor_dest, xor_srcs, 0, count, STRIPE_SIZE,
			   &sh->ops.zero_sum_result, &submit);

	atomic_inc(&sh->count);
	init_async_submit(&submit, ASYNC_TX_ACK, tx, ops_complete_check, sh, NULL);
	tx = async_trigger_callback(&submit);
}

static void ops_run_check_pq(struct stripe_head *sh, struct raid5_percpu *percpu, int checkp)
{
	struct page **srcs = percpu->scribble;
	struct async_submit_ctl submit;
	int count;

	pr_debug("%s: stripe %llu checkp: %d\n", __func__,
		(unsigned long long)sh->sector, checkp);

	count = set_syndrome_sources(srcs, sh);
	if (!checkp)
		srcs[count] = NULL;

	atomic_inc(&sh->count);
	init_async_submit(&submit, ASYNC_TX_ACK, NULL, ops_complete_check,
			  sh, to_addr_conv(sh, percpu));
	async_syndrome_val(srcs, 0, count+2, STRIPE_SIZE,
			   &sh->ops.zero_sum_result, percpu->spare_page, &submit);
}

static void raid_run_ops(struct stripe_head *sh, unsigned long ops_request)
{
	int overlap_clear = 0, i, disks = sh->disks;
	struct dma_async_tx_descriptor *tx = NULL;
	struct r5conf *conf = sh->raid_conf;
	int level = conf->level;
	struct raid5_percpu *percpu;
	unsigned long cpu;

	cpu = get_cpu();
	percpu = per_cpu_ptr(conf->percpu, cpu);
	if (test_bit(STRIPE_OP_BIOFILL, &ops_request)) {
		ops_run_biofill(sh);
		overlap_clear++;
	}

	if (test_bit(STRIPE_OP_COMPUTE_BLK, &ops_request)) {
		if (level < 6)
			tx = ops_run_compute5(sh, percpu);
		else {
			if (sh->ops.target2 < 0 || sh->ops.target < 0)
				tx = ops_run_compute6_1(sh, percpu);
			else
				tx = ops_run_compute6_2(sh, percpu);
		}
		/* terminate the chain if reconstruct is not set to be run */
		if (tx && !test_bit(STRIPE_OP_RECONSTRUCT, &ops_request))
			async_tx_ack(tx);
	}

	if (test_bit(STRIPE_OP_PREXOR, &ops_request))
		tx = ops_run_prexor(sh, percpu, tx);

	if (test_bit(STRIPE_OP_BIODRAIN, &ops_request)) {
		tx = ops_run_biodrain(sh, tx);
		overlap_clear++;
	}

	if (test_bit(STRIPE_OP_RECONSTRUCT, &ops_request)) {
		if (level < 6)
			ops_run_reconstruct5(sh, percpu, tx);
		else
			ops_run_reconstruct6(sh, percpu, tx);
	}

	if (test_bit(STRIPE_OP_CHECK, &ops_request)) {
		if (sh->check_state == check_state_run)
			ops_run_check_p(sh, percpu);
		else if (sh->check_state == check_state_run_q)
			ops_run_check_pq(sh, percpu, 0);
		else if (sh->check_state == check_state_run_pq)
			ops_run_check_pq(sh, percpu, 1);
		else
			BUG();
	}

	if (overlap_clear)
		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if (test_and_clear_bit(R5_Overlap, &dev->flags))
				wake_up(&sh->raid_conf->wait_for_overlap);
		}
	put_cpu();
}

static int grow_one_stripe(struct r5conf *conf)
{
	struct stripe_head *sh;
	sh = kmem_cache_zalloc(conf->slab_cache, GFP_KERNEL);
	if (!sh)
		return 0;

	sh->raid_conf = conf;

	spin_lock_init(&sh->stripe_lock);

	if (grow_buffers(sh)) {
		shrink_buffers(sh);
		kmem_cache_free(conf->slab_cache, sh);
		return 0;
	}
	/* we just created an active stripe so... */
	atomic_set(&sh->count, 1);
	atomic_inc(&conf->active_stripes);
	INIT_LIST_HEAD(&sh->lru);
	release_stripe(sh);
	return 1;
}

static int grow_stripes(struct r5conf *conf, int num)
{
	struct kmem_cache *sc;
	int devs = max(conf->raid_disks, conf->previous_raid_disks);

	if (conf->mddev->gendisk)
		sprintf(conf->cache_name[0],
			"raid%d-%s", conf->level, mdname(conf->mddev));
	else
		sprintf(conf->cache_name[0],
			"raid%d-%p", conf->level, conf->mddev);
	sprintf(conf->cache_name[1], "%s-alt", conf->cache_name[0]);

	conf->active_name = 0;
	sc = kmem_cache_create(conf->cache_name[conf->active_name],
			       sizeof(struct stripe_head)+(devs-1)*sizeof(struct r5dev),
			       0, 0, NULL);
	if (!sc)
		return 1;
	conf->slab_cache = sc;
	conf->pool_size = devs;
	while (num--)
		if (!grow_one_stripe(conf))
			return 1;
	return 0;
}

/**
 * scribble_len - return the required size of the scribble region
 * @num - total number of disks in the array
 *
 * The size must be enough to contain:
 * 1/ a struct page pointer for each device in the array +2
 * 2/ room to convert each entry in (1) to its corresponding dma
 *    (dma_map_page()) or page (page_address()) address.
 *
 * Note: the +2 is for the destination buffers of the ddf/raid6 case where we
 * calculate over all devices (not just the data blocks), using zeros in place
 * of the P and Q blocks.
 */
static size_t scribble_len(int num)
{
	size_t len;

	len = sizeof(struct page *) * (num+2) + sizeof(addr_conv_t) * (num+2);

	return len;
}

static int resize_stripes(struct r5conf *conf, int newsize)
{
	/* Make all the stripes able to hold 'newsize' devices.
	 * New slots in each stripe get 'page' set to a new page.
	 *
	 * This happens in stages:
	 * 1/ create a new kmem_cache and allocate the required number of
	 *    stripe_heads.
	 * 2/ gather all the old stripe_heads and transfer the pages across
	 *    to the new stripe_heads.  This will have the side effect of
	 *    freezing the array as once all stripe_heads have been collected,
	 *    no IO will be possible.  Old stripe heads are freed once their
	 *    pages have been transferred over, and the old kmem_cache is
	 *    freed when all stripes are done.
	 * 3/ reallocate conf->disks to be suitable bigger.  If this fails,
	 *    we simple return a failre status - no need to clean anything up.
	 * 4/ allocate new pages for the new slots in the new stripe_heads.
	 *    If this fails, we don't bother trying the shrink the
	 *    stripe_heads down again, we just leave them as they are.
	 *    As each stripe_head is processed the new one is released into
	 *    active service.
	 *
	 * Once step2 is started, we cannot afford to wait for a write,
	 * so we use GFP_NOIO allocations.
	 */
	struct stripe_head *osh, *nsh;
	LIST_HEAD(newstripes);
	struct disk_info *ndisks;
	unsigned long cpu;
	int err;
	struct kmem_cache *sc;
	int i;

	if (newsize <= conf->pool_size)
		return 0; /* never bother to shrink */

	err = md_allow_write(conf->mddev);
	if (err)
		return err;

	/* Step 1 */
	sc = kmem_cache_create(conf->cache_name[1-conf->active_name],
			       sizeof(struct stripe_head)+(newsize-1)*sizeof(struct r5dev),
			       0, 0, NULL);
	if (!sc)
		return -ENOMEM;

	for (i = conf->max_nr_stripes; i; i--) {
		nsh = kmem_cache_zalloc(sc, GFP_KERNEL);
		if (!nsh)
			break;

		nsh->raid_conf = conf;
		spin_lock_init(&nsh->stripe_lock);

		list_add(&nsh->lru, &newstripes);
	}
	if (i) {
		/* didn't get enough, give up */
		while (!list_empty(&newstripes)) {
			nsh = list_entry(newstripes.next, struct stripe_head, lru);
			list_del(&nsh->lru);
			kmem_cache_free(sc, nsh);
		}
		kmem_cache_destroy(sc);
		return -ENOMEM;
	}
	/* Step 2 - Must use GFP_NOIO now.
	 * OK, we have enough stripes, start collecting inactive
	 * stripes and copying them over
	 */
	list_for_each_entry(nsh, &newstripes, lru) {
		spin_lock_irq(&conf->device_lock);
		wait_event_lock_irq(conf->wait_for_stripe,
				    !list_empty(&conf->inactive_list),
				    conf->device_lock);
		osh = get_free_stripe(conf);
		spin_unlock_irq(&conf->device_lock);
		atomic_set(&nsh->count, 1);
		for(i=0; i<conf->pool_size; i++)
			nsh->dev[i].page = osh->dev[i].page;
		for( ; i<newsize; i++)
			nsh->dev[i].page = NULL;
		kmem_cache_free(conf->slab_cache, osh);
	}
	kmem_cache_destroy(conf->slab_cache);

	/* Step 3.
	 * At this point, we are holding all the stripes so the array
	 * is completely stalled, so now is a good time to resize
	 * conf->disks and the scribble region
	 */
	ndisks = kzalloc(newsize * sizeof(struct disk_info), GFP_NOIO);
	if (ndisks) {
		for (i=0; i<conf->raid_disks; i++)
			ndisks[i] = conf->disks[i];
		kfree(conf->disks);
		conf->disks = ndisks;
	} else
		err = -ENOMEM;

	get_online_cpus();
	conf->scribble_len = scribble_len(newsize);
	for_each_present_cpu(cpu) {
		struct raid5_percpu *percpu;
		void *scribble;

		percpu = per_cpu_ptr(conf->percpu, cpu);
		scribble = kmalloc(conf->scribble_len, GFP_NOIO);

		if (scribble) {
			kfree(percpu->scribble);
			percpu->scribble = scribble;
		} else {
			err = -ENOMEM;
			break;
		}
	}
	put_online_cpus();

	/* Step 4, return new stripes to service */
	while(!list_empty(&newstripes)) {
		nsh = list_entry(newstripes.next, struct stripe_head, lru);
		list_del_init(&nsh->lru);

		for (i=conf->raid_disks; i < newsize; i++)
			if (nsh->dev[i].page == NULL) {
				struct page *p = alloc_page(GFP_NOIO);
				nsh->dev[i].page = p;
				if (!p)
					err = -ENOMEM;
			}
		release_stripe(nsh);
	}
	/* critical section pass, GFP_NOIO no longer needed */

	conf->slab_cache = sc;
	conf->active_name = 1-conf->active_name;
	conf->pool_size = newsize;
	return err;
}

static int drop_one_stripe(struct r5conf *conf)
{
	struct stripe_head *sh;

	spin_lock_irq(&conf->device_lock);
	sh = get_free_stripe(conf);
	spin_unlock_irq(&conf->device_lock);
	if (!sh)
		return 0;
	BUG_ON(atomic_read(&sh->count));
	shrink_buffers(sh);
	kmem_cache_free(conf->slab_cache, sh);
	atomic_dec(&conf->active_stripes);
	return 1;
}

static void shrink_stripes(struct r5conf *conf)
{
	while (drop_one_stripe(conf))
		;

	if (conf->slab_cache)
		kmem_cache_destroy(conf->slab_cache);
	conf->slab_cache = NULL;
}

static void raid5_end_read_request(struct bio * bi, int error)
{
	struct stripe_head *sh = bi->bi_private;
	struct r5conf *conf = sh->raid_conf;
	int disks = sh->disks, i;
	int uptodate = test_bit(BIO_UPTODATE, &bi->bi_flags);
	char b[BDEVNAME_SIZE];
	struct md_rdev *rdev = NULL;
	sector_t s;

	for (i=0 ; i<disks; i++)
		if (bi == &sh->dev[i].req)
			break;

	pr_debug("end_read_request %llu/%d, count: %d, uptodate %d.\n",
		(unsigned long long)sh->sector, i, atomic_read(&sh->count),
		uptodate);
	if (i == disks) {
		BUG();
		return;
	}
	if (test_bit(R5_ReadRepl, &sh->dev[i].flags))
		/* If replacement finished while this request was outstanding,
		 * 'replacement' might be NULL already.
		 * In that case it moved down to 'rdev'.
		 * rdev is not removed until all requests are finished.
		 */
		rdev = conf->disks[i].replacement;
	if (!rdev)
		rdev = conf->disks[i].rdev;

	if (use_new_offset(conf, sh))
		s = sh->sector + rdev->new_data_offset;
	else
		s = sh->sector + rdev->data_offset;
	if (uptodate) {
		set_bit(R5_UPTODATE, &sh->dev[i].flags);
		if (test_bit(R5_ReadError, &sh->dev[i].flags)) {
			/* Note that this cannot happen on a
			 * replacement device.  We just fail those on
			 * any error
			 */
			printk_ratelimited(
				KERN_INFO
				"md/raid:%s: read error corrected"
				" (%lu sectors at %llu on %s)\n",
				mdname(conf->mddev), STRIPE_SECTORS,
				(unsigned long long)s,
				bdevname(rdev->bdev, b));
			atomic_add(STRIPE_SECTORS, &rdev->corrected_errors);
			clear_bit(R5_ReadError, &sh->dev[i].flags);
			clear_bit(R5_ReWrite, &sh->dev[i].flags);
		} else if (test_bit(R5_ReadNoMerge, &sh->dev[i].flags))
			clear_bit(R5_ReadNoMerge, &sh->dev[i].flags);

		if (atomic_read(&rdev->read_errors))
			atomic_set(&rdev->read_errors, 0);
	} else {
		const char *bdn = bdevname(rdev->bdev, b);
		int retry = 0;
		int set_bad = 0;

		clear_bit(R5_UPTODATE, &sh->dev[i].flags);
		atomic_inc(&rdev->read_errors);
		if (test_bit(R5_ReadRepl, &sh->dev[i].flags))
			printk_ratelimited(
				KERN_WARNING
				"md/raid:%s: read error on replacement device "
				"(sector %llu on %s).\n",
				mdname(conf->mddev),
				(unsigned long long)s,
				bdn);
		else if (conf->mddev->degraded >= conf->max_degraded) {
			set_bad = 1;
			printk_ratelimited(
				KERN_WARNING
				"md/raid:%s: read error not correctable "
				"(sector %llu on %s).\n",
				mdname(conf->mddev),
				(unsigned long long)s,
				bdn);
		} else if (test_bit(R5_ReWrite, &sh->dev[i].flags)) {
			/* Oh, no!!! */
			set_bad = 1;
			printk_ratelimited(
				KERN_WARNING
				"md/raid:%s: read error NOT corrected!! "
				"(sector %llu on %s).\n",
				mdname(conf->mddev),
				(unsigned long long)s,
				bdn);
		} else if (atomic_read(&rdev->read_errors)
			 > conf->max_nr_stripes)
			printk(KERN_WARNING
			       "md/raid:%s: Too many read errors, failing device %s.\n",
			       mdname(conf->mddev), bdn);
		else
			retry = 1;
		if (retry)
			if (test_bit(R5_ReadNoMerge, &sh->dev[i].flags)) {
				set_bit(R5_ReadError, &sh->dev[i].flags);
				clear_bit(R5_ReadNoMerge, &sh->dev[i].flags);
			} else
				set_bit(R5_ReadNoMerge, &sh->dev[i].flags);
		else {
			clear_bit(R5_ReadError, &sh->dev[i].flags);
			clear_bit(R5_ReWrite, &sh->dev[i].flags);
			if (!(set_bad
			      && test_bit(In_sync, &rdev->flags)
			      && rdev_set_badblocks(
				      rdev, sh->sector, STRIPE_SECTORS, 0)))
				md_error(conf->mddev, rdev);
		}
	}
	rdev_dec_pending(rdev, conf->mddev);
	clear_bit(R5_LOCKED, &sh->dev[i].flags);
	set_bit(STRIPE_HANDLE, &sh->state);
	release_stripe(sh);
}

static void raid5_end_write_request(struct bio *bi, int error)
{
	struct stripe_head *sh = bi->bi_private;
	struct r5conf *conf = sh->raid_conf;
	int disks = sh->disks, i;
	struct md_rdev *uninitialized_var(rdev);
	int uptodate = test_bit(BIO_UPTODATE, &bi->bi_flags);
	sector_t first_bad;
	int bad_sectors;
	int replacement = 0;

	for (i = 0 ; i < disks; i++) {
		if (bi == &sh->dev[i].req) {
			rdev = conf->disks[i].rdev;
			break;
		}
		if (bi == &sh->dev[i].rreq) {
			rdev = conf->disks[i].replacement;
			if (rdev)
				replacement = 1;
			else
				/* rdev was removed and 'replacement'
				 * replaced it.  rdev is not removed
				 * until all requests are finished.
				 */
				rdev = conf->disks[i].rdev;
			break;
		}
	}
	pr_debug("end_write_request %llu/%d, count %d, uptodate: %d.\n",
		(unsigned long long)sh->sector, i, atomic_read(&sh->count),
		uptodate);
	if (i == disks) {
		BUG();
		return;
	}

	if (replacement) {
		if (!uptodate)
			md_error(conf->mddev, rdev);
		else if (is_badblock(rdev, sh->sector,
				     STRIPE_SECTORS,
				     &first_bad, &bad_sectors))
			set_bit(R5_MadeGoodRepl, &sh->dev[i].flags);
	} else {
		if (!uptodate) {
			set_bit(WriteErrorSeen, &rdev->flags);
			set_bit(R5_WriteError, &sh->dev[i].flags);
			if (!test_and_set_bit(WantReplacement, &rdev->flags))
				set_bit(MD_RECOVERY_NEEDED,
					&rdev->mddev->recovery);
		} else if (is_badblock(rdev, sh->sector,
				       STRIPE_SECTORS,
				       &first_bad, &bad_sectors)) {
			set_bit(R5_MadeGood, &sh->dev[i].flags);
			if (test_bit(R5_ReadError, &sh->dev[i].flags))
				/* That was a successful write so make
				 * sure it looks like we already did
				 * a re-write.
				 */
				set_bit(R5_ReWrite, &sh->dev[i].flags);
		}
	}
	rdev_dec_pending(rdev, conf->mddev);

	if (!test_and_clear_bit(R5_DOUBLE_LOCKED, &sh->dev[i].flags))
		clear_bit(R5_LOCKED, &sh->dev[i].flags);
	set_bit(STRIPE_HANDLE, &sh->state);
	release_stripe(sh);
}

static sector_t compute_blocknr(struct stripe_head *sh, int i, int previous);
	
static void raid5_build_block(struct stripe_head *sh, int i, int previous)
{
	struct r5dev *dev = &sh->dev[i];

	bio_init(&dev->req);
	dev->req.bi_io_vec = &dev->vec;
	dev->req.bi_vcnt++;
	dev->req.bi_max_vecs++;
	dev->req.bi_private = sh;
	dev->vec.bv_page = dev->page;

	bio_init(&dev->rreq);
	dev->rreq.bi_io_vec = &dev->rvec;
	dev->rreq.bi_vcnt++;
	dev->rreq.bi_max_vecs++;
	dev->rreq.bi_private = sh;
	dev->rvec.bv_page = dev->page;

	dev->flags = 0;
	dev->sector = compute_blocknr(sh, i, previous);
}

static void error(struct mddev *mddev, struct md_rdev *rdev)
{
	char b[BDEVNAME_SIZE];
	struct r5conf *conf = mddev->private;
	unsigned long flags;
	pr_debug("raid456: error called\n");

	spin_lock_irqsave(&conf->device_lock, flags);
	clear_bit(In_sync, &rdev->flags);
	mddev->degraded = calc_degraded(conf);
	spin_unlock_irqrestore(&conf->device_lock, flags);
	set_bit(MD_RECOVERY_INTR, &mddev->recovery);

	set_bit(Blocked, &rdev->flags);
	set_bit(Faulty, &rdev->flags);
	set_bit(MD_CHANGE_DEVS, &mddev->flags);
	printk(KERN_ALERT
	       "md/raid:%s: Disk failure on %s, disabling device.\n"
	       "md/raid:%s: Operation continuing on %d devices.\n",
	       mdname(mddev),
	       bdevname(rdev->bdev, b),
	       mdname(mddev),
	       conf->raid_disks - mddev->degraded);
}

/*
 * Input: a 'big' sector number,
 * Output: index of the data and parity disk, and the sector # in them.
 */
static sector_t raid5_compute_sector(struct r5conf *conf, sector_t r_sector,
				     int previous, int *dd_idx,
				     struct stripe_head *sh)
{
	sector_t stripe, stripe2;
	sector_t chunk_number;
	unsigned int chunk_offset;
	int pd_idx, qd_idx;
	int ddf_layout = 0;
	sector_t new_sector;
	int algorithm = previous ? conf->prev_algo
				 : conf->algorithm;
	int sectors_per_chunk = previous ? conf->prev_chunk_sectors
					 : conf->chunk_sectors;
	int raid_disks = previous ? conf->previous_raid_disks
				  : conf->raid_disks;
	int data_disks = raid_disks - conf->max_degraded;

	/* First compute the information on this sector */

	/*
	 * Compute the chunk number and the sector offset inside the chunk
	 */
	chunk_offset = sector_div(r_sector, sectors_per_chunk);
	chunk_number = r_sector;

	/*
	 * Compute the stripe number
	 */
	stripe = chunk_number;
	*dd_idx = sector_div(stripe, data_disks);
	stripe2 = stripe;
	/*
	 * Select the parity disk based on the user selected algorithm.
	 */
	pd_idx = qd_idx = -1;
	switch(conf->level) {
	case 4:
		pd_idx = data_disks;
		break;
	case 5:
		switch (algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
			pd_idx = data_disks - sector_div(stripe2, raid_disks);
			if (*dd_idx >= pd_idx)
				(*dd_idx)++;
			break;
		case ALGORITHM_RIGHT_ASYMMETRIC:
			pd_idx = sector_div(stripe2, raid_disks);
			if (*dd_idx >= pd_idx)
				(*dd_idx)++;
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
			pd_idx = data_disks - sector_div(stripe2, raid_disks);
			*dd_idx = (pd_idx + 1 + *dd_idx) % raid_disks;
			break;
		case ALGORITHM_RIGHT_SYMMETRIC:
			pd_idx = sector_div(stripe2, raid_disks);
			*dd_idx = (pd_idx + 1 + *dd_idx) % raid_disks;
			break;
		case ALGORITHM_PARITY_0:
			pd_idx = 0;
			(*dd_idx)++;
			break;
		case ALGORITHM_PARITY_N:
			pd_idx = data_disks;
			break;
		default:
			BUG();
		}
		break;
	case 6:

		switch (algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
			pd_idx = raid_disks - 1 - sector_div(stripe2, raid_disks);
			qd_idx = pd_idx + 1;
			if (pd_idx == raid_disks-1) {
				(*dd_idx)++;	/* Q D D D P */
				qd_idx = 0;
			} else if (*dd_idx >= pd_idx)
				(*dd_idx) += 2; /* D D P Q D */
			break;
		case ALGORITHM_RIGHT_ASYMMETRIC:
			pd_idx = sector_div(stripe2, raid_disks);
			qd_idx = pd_idx + 1;
			if (pd_idx == raid_disks-1) {
				(*dd_idx)++;	/* Q D D D P */
				qd_idx = 0;
			} else if (*dd_idx >= pd_idx)
				(*dd_idx) += 2; /* D D P Q D */
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
			pd_idx = raid_disks - 1 - sector_div(stripe2, raid_disks);
			qd_idx = (pd_idx + 1) % raid_disks;
			*dd_idx = (pd_idx + 2 + *dd_idx) % raid_disks;
			break;
		case ALGORITHM_RIGHT_SYMMETRIC:
			pd_idx = sector_div(stripe2, raid_disks);
			qd_idx = (pd_idx + 1) % raid_disks;
			*dd_idx = (pd_idx + 2 + *dd_idx) % raid_disks;
			break;

		case ALGORITHM_PARITY_0:
			pd_idx = 0;
			qd_idx = 1;
			(*dd_idx) += 2;
			break;
		case ALGORITHM_PARITY_N:
			pd_idx = data_disks;
			qd_idx = data_disks + 1;
			break;

		case ALGORITHM_ROTATING_ZERO_RESTART:
			/* Exactly the same as RIGHT_ASYMMETRIC, but or
			 * of blocks for computing Q is different.
			 */
			pd_idx = sector_div(stripe2, raid_disks);
			qd_idx = pd_idx + 1;
			if (pd_idx == raid_disks-1) {
				(*dd_idx)++;	/* Q D D D P */
				qd_idx = 0;
			} else if (*dd_idx >= pd_idx)
				(*dd_idx) += 2; /* D D P Q D */
			ddf_layout = 1;
			break;

		case ALGORITHM_ROTATING_N_RESTART:
			/* Same a left_asymmetric, by first stripe is
			 * D D D P Q  rather than
			 * Q D D D P
			 */
			stripe2 += 1;
			pd_idx = raid_disks - 1 - sector_div(stripe2, raid_disks);
			qd_idx = pd_idx + 1;
			if (pd_idx == raid_disks-1) {
				(*dd_idx)++;	/* Q D D D P */
				qd_idx = 0;
			} else if (*dd_idx >= pd_idx)
				(*dd_idx) += 2; /* D D P Q D */
			ddf_layout = 1;
			break;

		case ALGORITHM_ROTATING_N_CONTINUE:
			/* Same as left_symmetric but Q is before P */
			pd_idx = raid_disks - 1 - sector_div(stripe2, raid_disks);
			qd_idx = (pd_idx + raid_disks - 1) % raid_disks;
			*dd_idx = (pd_idx + 1 + *dd_idx) % raid_disks;
			ddf_layout = 1;
			break;

		case ALGORITHM_LEFT_ASYMMETRIC_6:
			/* RAID5 left_asymmetric, with Q on last device */
			pd_idx = data_disks - sector_div(stripe2, raid_disks-1);
			if (*dd_idx >= pd_idx)
				(*dd_idx)++;
			qd_idx = raid_disks - 1;
			break;

		case ALGORITHM_RIGHT_ASYMMETRIC_6:
			pd_idx = sector_div(stripe2, raid_disks-1);
			if (*dd_idx >= pd_idx)
				(*dd_idx)++;
			qd_idx = raid_disks - 1;
			break;

		case ALGORITHM_LEFT_SYMMETRIC_6:
			pd_idx = data_disks - sector_div(stripe2, raid_disks-1);
			*dd_idx = (pd_idx + 1 + *dd_idx) % (raid_disks-1);
			qd_idx = raid_disks - 1;
			break;

		case ALGORITHM_RIGHT_SYMMETRIC_6:
			pd_idx = sector_div(stripe2, raid_disks-1);
			*dd_idx = (pd_idx + 1 + *dd_idx) % (raid_disks-1);
			qd_idx = raid_disks - 1;
			break;

		case ALGORITHM_PARITY_0_6:
			pd_idx = 0;
			(*dd_idx)++;
			qd_idx = raid_disks - 1;
			break;

		default:
			BUG();
		}
		break;
	}

	if (sh) {
		sh->pd_idx = pd_idx;
		sh->qd_idx = qd_idx;
		sh->ddf_layout = ddf_layout;
	}
	/*
	 * Finally, compute the new sector number
	 */
	new_sector = (sector_t)stripe * sectors_per_chunk + chunk_offset;
	return new_sector;
}


static sector_t compute_blocknr(struct stripe_head *sh, int i, int previous)
{
	struct r5conf *conf = sh->raid_conf;
	int raid_disks = sh->disks;
	int data_disks = raid_disks - conf->max_degraded;
	sector_t new_sector = sh->sector, check;
	int sectors_per_chunk = previous ? conf->prev_chunk_sectors
					 : conf->chunk_sectors;
	int algorithm = previous ? conf->prev_algo
				 : conf->algorithm;
	sector_t stripe;
	int chunk_offset;
	sector_t chunk_number;
	int dummy1, dd_idx = i;
	sector_t r_sector;
	struct stripe_head sh2;


	chunk_offset = sector_div(new_sector, sectors_per_chunk);
	stripe = new_sector;

	if (i == sh->pd_idx)
		return 0;
	switch(conf->level) {
	case 4: break;
	case 5:
		switch (algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
		case ALGORITHM_RIGHT_ASYMMETRIC:
			if (i > sh->pd_idx)
				i--;
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
		case ALGORITHM_RIGHT_SYMMETRIC:
			if (i < sh->pd_idx)
				i += raid_disks;
			i -= (sh->pd_idx + 1);
			break;
		case ALGORITHM_PARITY_0:
			i -= 1;
			break;
		case ALGORITHM_PARITY_N:
			break;
		default:
			BUG();
		}
		break;
	case 6:
		if (i == sh->qd_idx)
			return 0; /* It is the Q disk */
		switch (algorithm) {
		case ALGORITHM_LEFT_ASYMMETRIC:
		case ALGORITHM_RIGHT_ASYMMETRIC:
		case ALGORITHM_ROTATING_ZERO_RESTART:
		case ALGORITHM_ROTATING_N_RESTART:
			if (sh->pd_idx == raid_disks-1)
				i--;	/* Q D D D P */
			else if (i > sh->pd_idx)
				i -= 2; /* D D P Q D */
			break;
		case ALGORITHM_LEFT_SYMMETRIC:
		case ALGORITHM_RIGHT_SYMMETRIC:
			if (sh->pd_idx == raid_disks-1)
				i--; /* Q D D D P */
			else {
				/* D D P Q D */
				if (i < sh->pd_idx)
					i += raid_disks;
				i -= (sh->pd_idx + 2);
			}
			break;
		case ALGORITHM_PARITY_0:
			i -= 2;
			break;
		case ALGORITHM_PARITY_N:
			break;
		case ALGORITHM_ROTATING_N_CONTINUE:
			/* Like left_symmetric, but P is before Q */
			if (sh->pd_idx == 0)
				i--;	/* P D D D Q */
			else {
				/* D D Q P D */
				if (i < sh->pd_idx)
					i += raid_disks;
				i -= (sh->pd_idx + 1);
			}
			break;
		case ALGORITHM_LEFT_ASYMMETRIC_6:
		case ALGORITHM_RIGHT_ASYMMETRIC_6:
			if (i > sh->pd_idx)
				i--;
			break;
		case ALGORITHM_LEFT_SYMMETRIC_6:
		case ALGORITHM_RIGHT_SYMMETRIC_6:
			if (i < sh->pd_idx)
				i += data_disks + 1;
			i -= (sh->pd_idx + 1);
			break;
		case ALGORITHM_PARITY_0_6:
			i -= 1;
			break;
		default:
			BUG();
		}
		break;
	}

	chunk_number = stripe * data_disks + i;
	r_sector = chunk_number * sectors_per_chunk + chunk_offset;

	check = raid5_compute_sector(conf, r_sector,
				     previous, &dummy1, &sh2);
	if (check != sh->sector || dummy1 != dd_idx || sh2.pd_idx != sh->pd_idx
		|| sh2.qd_idx != sh->qd_idx) {
		printk(KERN_ERR "md/raid:%s: compute_blocknr: map not correct\n",
		       mdname(conf->mddev));
		return 0;
	}
	return r_sector;
}


static void
schedule_reconstruction(struct stripe_head *sh, struct stripe_head_state *s,
			 int rcw, int expand)
{
	int i, pd_idx = sh->pd_idx, disks = sh->disks;
	struct r5conf *conf = sh->raid_conf;
	int level = conf->level;

	if (rcw) {

		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];

			if (dev->towrite) {
				set_bit(R5_LOCKED, &dev->flags);
				set_bit(R5_Wantdrain, &dev->flags);
				if (!expand)
					clear_bit(R5_UPTODATE, &dev->flags);
				s->locked++;
			}
		}
		/* if we are not expanding this is a proper write request, and
		 * there will be bios with new data to be drained into the
		 * stripe cache
		 */
		if (!expand) {
			if (!s->locked)
				/* False alarm, nothing to do */
				return;
			sh->reconstruct_state = reconstruct_state_drain_run;
			set_bit(STRIPE_OP_BIODRAIN, &s->ops_request);
		} else
			sh->reconstruct_state = reconstruct_state_run;

		set_bit(STRIPE_OP_RECONSTRUCT, &s->ops_request);

		if (s->locked + conf->max_degraded == disks)
			if (!test_and_set_bit(STRIPE_FULL_WRITE, &sh->state))
				atomic_inc(&conf->pending_full_writes);
	} else {
		BUG_ON(level == 6);
		BUG_ON(!(test_bit(R5_UPTODATE, &sh->dev[pd_idx].flags) ||
			test_bit(R5_Wantcompute, &sh->dev[pd_idx].flags)));

		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if (i == pd_idx)
				continue;

			if (dev->towrite &&
			    (test_bit(R5_UPTODATE, &dev->flags) ||
			     test_bit(R5_Wantcompute, &dev->flags))) {
				set_bit(R5_Wantdrain, &dev->flags);
				set_bit(R5_LOCKED, &dev->flags);
				clear_bit(R5_UPTODATE, &dev->flags);
				s->locked++;
			}
		}
		if (!s->locked)
			/* False alarm - nothing to do */
			return;
		sh->reconstruct_state = reconstruct_state_prexor_drain_run;
		set_bit(STRIPE_OP_PREXOR, &s->ops_request);
		set_bit(STRIPE_OP_BIODRAIN, &s->ops_request);
		set_bit(STRIPE_OP_RECONSTRUCT, &s->ops_request);
	}

	/* keep the parity disk(s) locked while asynchronous operations
	 * are in flight
	 */
	set_bit(R5_LOCKED, &sh->dev[pd_idx].flags);
	clear_bit(R5_UPTODATE, &sh->dev[pd_idx].flags);
	s->locked++;

	if (level == 6) {
		int qd_idx = sh->qd_idx;
		struct r5dev *dev = &sh->dev[qd_idx];

		set_bit(R5_LOCKED, &dev->flags);
		clear_bit(R5_UPTODATE, &dev->flags);
		s->locked++;
	}

	pr_debug("%s: stripe %llu locked: %d ops_request: %lx\n",
		__func__, (unsigned long long)sh->sector,
		s->locked, s->ops_request);
}

/*
 * Each stripe/dev can have one or more bion attached.
 * toread/towrite point to the first in a chain.
 * The bi_next chain must be in order.
 */
static int add_stripe_bio(struct stripe_head *sh, struct bio *bi, int dd_idx, int forwrite)
{
	struct bio **bip;
	struct r5conf *conf = sh->raid_conf;
	int firstwrite=0;

	pr_debug("adding bi b#%llu to stripe s#%llu\n",
		(unsigned long long)bi->bi_sector,
		(unsigned long long)sh->sector);

	/*
	 * If several bio share a stripe. The bio bi_phys_segments acts as a
	 * reference count to avoid race. The reference count should already be
	 * increased before this function is called (for example, in
	 * make_request()), so other bio sharing this stripe will not free the
	 * stripe. If a stripe is owned by one stripe, the stripe lock will
	 * protect it.
	 */
	spin_lock_irq(&sh->stripe_lock);
	if (forwrite) {
		bip = &sh->dev[dd_idx].towrite;
		if (*bip == NULL)
			firstwrite = 1;
	} else
		bip = &sh->dev[dd_idx].toread;
	while (*bip && (*bip)->bi_sector < bi->bi_sector) {
		if (bio_end_sector(*bip) > bi->bi_sector)
			goto overlap;
		bip = & (*bip)->bi_next;
	}
	if (*bip && (*bip)->bi_sector < bio_end_sector(bi))
		goto overlap;

	BUG_ON(*bip && bi->bi_next && (*bip) != bi->bi_next);
	if (*bip)
		bi->bi_next = *bip;
	*bip = bi;
	raid5_inc_bi_active_stripes(bi);

	if (forwrite) {
		/* check if page is covered */
		sector_t sector = sh->dev[dd_idx].sector;
		for (bi=sh->dev[dd_idx].towrite;
		     sector < sh->dev[dd_idx].sector + STRIPE_SECTORS &&
			     bi && bi->bi_sector <= sector;
		     bi = r5_next_bio(bi, sh->dev[dd_idx].sector)) {
			if (bio_end_sector(bi) >= sector)
				sector = bio_end_sector(bi);
		}
		if (sector >= sh->dev[dd_idx].sector + STRIPE_SECTORS)
			set_bit(R5_OVERWRITE, &sh->dev[dd_idx].flags);
	}

	pr_debug("added bi b#%llu to stripe s#%llu, disk %d.\n",
		(unsigned long long)(*bip)->bi_sector,
		(unsigned long long)sh->sector, dd_idx);
	spin_unlock_irq(&sh->stripe_lock);

	if (conf->mddev->bitmap && firstwrite) {
		bitmap_startwrite(conf->mddev->bitmap, sh->sector,
				  STRIPE_SECTORS, 0);
		sh->bm_seq = conf->seq_flush+1;
		set_bit(STRIPE_BIT_DELAY, &sh->state);
	}
	return 1;

 overlap:
	set_bit(R5_Overlap, &sh->dev[dd_idx].flags);
	spin_unlock_irq(&sh->stripe_lock);
	return 0;
}

static void end_reshape(struct r5conf *conf);

static void stripe_set_idx(sector_t stripe, struct r5conf *conf, int previous,
			    struct stripe_head *sh)
{
	int sectors_per_chunk =
		previous ? conf->prev_chunk_sectors : conf->chunk_sectors;
	int dd_idx;
	int chunk_offset = sector_div(stripe, sectors_per_chunk);
	int disks = previous ? conf->previous_raid_disks : conf->raid_disks;

	raid5_compute_sector(conf,
			     stripe * (disks - conf->max_degraded)
			     *sectors_per_chunk + chunk_offset,
			     previous,
			     &dd_idx, sh);
}

static void
handle_failed_stripe(struct r5conf *conf, struct stripe_head *sh,
				struct stripe_head_state *s, int disks,
				struct bio **return_bi)
{
	int i;
	for (i = disks; i--; ) {
		struct bio *bi;
		int bitmap_end = 0;

		if (test_bit(R5_ReadError, &sh->dev[i].flags)) {
			struct md_rdev *rdev;
			rcu_read_lock();
			rdev = rcu_dereference(conf->disks[i].rdev);
			if (rdev && test_bit(In_sync, &rdev->flags))
				atomic_inc(&rdev->nr_pending);
			else
				rdev = NULL;
			rcu_read_unlock();
			if (rdev) {
				if (!rdev_set_badblocks(
					    rdev,
					    sh->sector,
					    STRIPE_SECTORS, 0))
					md_error(conf->mddev, rdev);
				rdev_dec_pending(rdev, conf->mddev);
			}
		}
		spin_lock_irq(&sh->stripe_lock);
		/* fail all writes first */
		bi = sh->dev[i].towrite;
		sh->dev[i].towrite = NULL;
		spin_unlock_irq(&sh->stripe_lock);
		if (bi)
			bitmap_end = 1;

		if (test_and_clear_bit(R5_Overlap, &sh->dev[i].flags))
			wake_up(&conf->wait_for_overlap);

		while (bi && bi->bi_sector <
			sh->dev[i].sector + STRIPE_SECTORS) {
			struct bio *nextbi = r5_next_bio(bi, sh->dev[i].sector);
			clear_bit(BIO_UPTODATE, &bi->bi_flags);
			if (!raid5_dec_bi_active_stripes(bi)) {
				md_write_end(conf->mddev);
				bi->bi_next = *return_bi;
				*return_bi = bi;
			}
			bi = nextbi;
		}
		if (bitmap_end)
			bitmap_endwrite(conf->mddev->bitmap, sh->sector,
				STRIPE_SECTORS, 0, 0);
		bitmap_end = 0;
		/* and fail all 'written' */
		bi = sh->dev[i].written;
		sh->dev[i].written = NULL;
		if (bi) bitmap_end = 1;
		while (bi && bi->bi_sector <
		       sh->dev[i].sector + STRIPE_SECTORS) {
			struct bio *bi2 = r5_next_bio(bi, sh->dev[i].sector);
			clear_bit(BIO_UPTODATE, &bi->bi_flags);
			if (!raid5_dec_bi_active_stripes(bi)) {
				md_write_end(conf->mddev);
				bi->bi_next = *return_bi;
				*return_bi = bi;
			}
			bi = bi2;
		}

		/* fail any reads if this device is non-operational and
		 * the data has not reached the cache yet.
		 */
		if (!test_bit(R5_Wantfill, &sh->dev[i].flags) &&
		    (!test_bit(R5_Insync, &sh->dev[i].flags) ||
		      test_bit(R5_ReadError, &sh->dev[i].flags))) {
			spin_lock_irq(&sh->stripe_lock);
			bi = sh->dev[i].toread;
			sh->dev[i].toread = NULL;
			spin_unlock_irq(&sh->stripe_lock);
			if (test_and_clear_bit(R5_Overlap, &sh->dev[i].flags))
				wake_up(&conf->wait_for_overlap);
			while (bi && bi->bi_sector <
			       sh->dev[i].sector + STRIPE_SECTORS) {
				struct bio *nextbi =
					r5_next_bio(bi, sh->dev[i].sector);
				clear_bit(BIO_UPTODATE, &bi->bi_flags);
				if (!raid5_dec_bi_active_stripes(bi)) {
					bi->bi_next = *return_bi;
					*return_bi = bi;
				}
				bi = nextbi;
			}
		}
		if (bitmap_end)
			bitmap_endwrite(conf->mddev->bitmap, sh->sector,
					STRIPE_SECTORS, 0, 0);
		/* If we were in the middle of a write the parity block might
		 * still be locked - so just clear all R5_LOCKED flags
		 */
		clear_bit(R5_LOCKED, &sh->dev[i].flags);
	}

	if (test_and_clear_bit(STRIPE_FULL_WRITE, &sh->state))
		if (atomic_dec_and_test(&conf->pending_full_writes))
			md_wakeup_thread(conf->mddev->thread);
}

static void
handle_failed_sync(struct r5conf *conf, struct stripe_head *sh,
		   struct stripe_head_state *s)
{
	int abort = 0;
	int i;

	clear_bit(STRIPE_SYNCING, &sh->state);
	if (test_and_clear_bit(R5_Overlap, &sh->dev[sh->pd_idx].flags))
		wake_up(&conf->wait_for_overlap);
	s->syncing = 0;
	s->replacing = 0;
	/* There is nothing more to do for sync/check/repair.
	 * Don't even need to abort as that is handled elsewhere
	 * if needed, and not always wanted e.g. if there is a known
	 * bad block here.
	 * For recover/replace we need to record a bad block on all
	 * non-sync devices, or abort the recovery
	 */
	if (test_bit(MD_RECOVERY_RECOVER, &conf->mddev->recovery)) {
		/* During recovery devices cannot be removed, so
		 * locking and refcounting of rdevs is not needed
		 */
		for (i = 0; i < conf->raid_disks; i++) {
			struct md_rdev *rdev = conf->disks[i].rdev;
			if (rdev
			    && !test_bit(Faulty, &rdev->flags)
			    && !test_bit(In_sync, &rdev->flags)
			    && !rdev_set_badblocks(rdev, sh->sector,
						   STRIPE_SECTORS, 0))
				abort = 1;
			rdev = conf->disks[i].replacement;
			if (rdev
			    && !test_bit(Faulty, &rdev->flags)
			    && !test_bit(In_sync, &rdev->flags)
			    && !rdev_set_badblocks(rdev, sh->sector,
						   STRIPE_SECTORS, 0))
				abort = 1;
		}
		if (abort)
			conf->recovery_disabled =
				conf->mddev->recovery_disabled;
	}
	md_done_sync(conf->mddev, STRIPE_SECTORS, !abort);
}

static int want_replace(struct stripe_head *sh, int disk_idx)
{
	struct md_rdev *rdev;
	int rv = 0;
	/* Doing recovery so rcu locking not required */
	rdev = sh->raid_conf->disks[disk_idx].replacement;
	if (rdev
	    && !test_bit(Faulty, &rdev->flags)
	    && !test_bit(In_sync, &rdev->flags)
	    && (rdev->recovery_offset <= sh->sector
		|| rdev->mddev->recovery_cp <= sh->sector))
		rv = 1;

	return rv;
}

/* fetch_block - checks the given member device to see if its data needs
 * to be read or computed to satisfy a request.
 *
 * Returns 1 when no more member devices need to be checked, otherwise returns
 * 0 to tell the loop in handle_stripe_fill to continue
 */
static int fetch_block(struct stripe_head *sh, struct stripe_head_state *s,
		       int disk_idx, int disks)
{
	struct r5dev *dev = &sh->dev[disk_idx];
	struct r5dev *fdev[2] = { &sh->dev[s->failed_num[0]],
				  &sh->dev[s->failed_num[1]] };

	/* is the data in this block needed, and can we get it? */
	if (!test_bit(R5_LOCKED, &dev->flags) &&
	    !test_bit(R5_UPTODATE, &dev->flags) &&
	    (dev->toread ||
	     (dev->towrite && !test_bit(R5_OVERWRITE, &dev->flags)) ||
	     s->syncing || s->expanding ||
	     (s->replacing && want_replace(sh, disk_idx)) ||
	     (s->failed >= 1 && fdev[0]->toread) ||
	     (s->failed >= 2 && fdev[1]->toread) ||
	     (sh->raid_conf->level <= 5 && s->failed && fdev[0]->towrite &&
	      !test_bit(R5_OVERWRITE, &fdev[0]->flags)) ||
	     (sh->raid_conf->level == 6 && s->failed && s->to_write))) {
		/* we would like to get this block, possibly by computing it,
		 * otherwise read it if the backing disk is insync
		 */
		BUG_ON(test_bit(R5_Wantcompute, &dev->flags));
		BUG_ON(test_bit(R5_Wantread, &dev->flags));
		if ((s->uptodate == disks - 1) &&
		    (s->failed && (disk_idx == s->failed_num[0] ||
				   disk_idx == s->failed_num[1]))) {
			/* have disk failed, and we're requested to fetch it;
			 * do compute it
			 */
			pr_debug("Computing stripe %llu block %d\n",
			       (unsigned long long)sh->sector, disk_idx);
			set_bit(STRIPE_COMPUTE_RUN, &sh->state);
			set_bit(STRIPE_OP_COMPUTE_BLK, &s->ops_request);
			set_bit(R5_Wantcompute, &dev->flags);
			sh->ops.target = disk_idx;
			sh->ops.target2 = -1; /* no 2nd target */
			s->req_compute = 1;
			/* Careful: from this point on 'uptodate' is in the eye
			 * of raid_run_ops which services 'compute' operations
			 * before writes. R5_Wantcompute flags a block that will
			 * be R5_UPTODATE by the time it is needed for a
			 * subsequent operation.
			 */
			s->uptodate++;
			return 1;
		} else if (s->uptodate == disks-2 && s->failed >= 2) {
			/* Computing 2-failure is *very* expensive; only
			 * do it if failed >= 2
			 */
			int other;
			for (other = disks; other--; ) {
				if (other == disk_idx)
					continue;
				if (!test_bit(R5_UPTODATE,
				      &sh->dev[other].flags))
					break;
			}
			BUG_ON(other < 0);
			pr_debug("Computing stripe %llu blocks %d,%d\n",
			       (unsigned long long)sh->sector,
			       disk_idx, other);
			set_bit(STRIPE_COMPUTE_RUN, &sh->state);
			set_bit(STRIPE_OP_COMPUTE_BLK, &s->ops_request);
			set_bit(R5_Wantcompute, &sh->dev[disk_idx].flags);
			set_bit(R5_Wantcompute, &sh->dev[other].flags);
			sh->ops.target = disk_idx;
			sh->ops.target2 = other;
			s->uptodate += 2;
			s->req_compute = 1;
			return 1;
		} else if (test_bit(R5_Insync, &dev->flags)) {
			set_bit(R5_LOCKED, &dev->flags);
			set_bit(R5_Wantread, &dev->flags);
			s->locked++;
			pr_debug("Reading block %d (sync=%d)\n",
				disk_idx, s->syncing);
		}
	}

	return 0;
}

/**
 * handle_stripe_fill - read or compute data to satisfy pending requests.
 */
static void handle_stripe_fill(struct stripe_head *sh,
			       struct stripe_head_state *s,
			       int disks)
{
	int i;

	/* look for blocks to read/compute, skip this if a compute
	 * is already in flight, or if the stripe contents are in the
	 * midst of changing due to a write
	 */
	if (!test_bit(STRIPE_COMPUTE_RUN, &sh->state) && !sh->check_state &&
	    !sh->reconstruct_state)
		for (i = disks; i--; )
			if (fetch_block(sh, s, i, disks))
				break;
	set_bit(STRIPE_HANDLE, &sh->state);
}


/* handle_stripe_clean_event
 * any written block on an uptodate or failed drive can be returned.
 * Note that if we 'wrote' to a failed drive, it will be UPTODATE, but
 * never LOCKED, so we don't need to test 'failed' directly.
 */
static void handle_stripe_clean_event(struct r5conf *conf,
	struct stripe_head *sh, int disks, struct bio **return_bi)
{
	int i;
	struct r5dev *dev;
	int discard_pending = 0;

	for (i = disks; i--; )
		if (sh->dev[i].written) {
			dev = &sh->dev[i];
			if (!test_bit(R5_LOCKED, &dev->flags) &&
			    (test_bit(R5_UPTODATE, &dev->flags) ||
			     test_bit(R5_Discard, &dev->flags))) {
				/* We can return any write requests */
				struct bio *wbi, *wbi2;
				pr_debug("Return write for disc %d\n", i);
				if (test_and_clear_bit(R5_Discard, &dev->flags))
					clear_bit(R5_UPTODATE, &dev->flags);
				wbi = dev->written;
				dev->written = NULL;
				while (wbi && wbi->bi_sector <
					dev->sector + STRIPE_SECTORS) {
					wbi2 = r5_next_bio(wbi, dev->sector);
					if (!raid5_dec_bi_active_stripes(wbi)) {
						md_write_end(conf->mddev);
						wbi->bi_next = *return_bi;
						*return_bi = wbi;
					}
					wbi = wbi2;
				}
				bitmap_endwrite(conf->mddev->bitmap, sh->sector,
						STRIPE_SECTORS,
					 !test_bit(STRIPE_DEGRADED, &sh->state),
						0);
			} else if (test_bit(R5_Discard, &dev->flags))
				discard_pending = 1;
		}
	if (!discard_pending &&
	    test_bit(R5_Discard, &sh->dev[sh->pd_idx].flags)) {
		clear_bit(R5_Discard, &sh->dev[sh->pd_idx].flags);
		clear_bit(R5_UPTODATE, &sh->dev[sh->pd_idx].flags);
		if (sh->qd_idx >= 0) {
			clear_bit(R5_Discard, &sh->dev[sh->qd_idx].flags);
			clear_bit(R5_UPTODATE, &sh->dev[sh->qd_idx].flags);
		}
		/* now that discard is done we can proceed with any sync */
		clear_bit(STRIPE_DISCARD, &sh->state);
		if (test_bit(STRIPE_SYNC_REQUESTED, &sh->state))
			set_bit(STRIPE_HANDLE, &sh->state);

	}

	if (test_and_clear_bit(STRIPE_FULL_WRITE, &sh->state))
		if (atomic_dec_and_test(&conf->pending_full_writes))
			md_wakeup_thread(conf->mddev->thread);
}

static void handle_stripe_dirtying(struct r5conf *conf,
				   struct stripe_head *sh,
				   struct stripe_head_state *s,
				   int disks)
{
	int rmw = 0, rcw = 0, i;
	sector_t recovery_cp = conf->mddev->recovery_cp;

	/* RAID6 requires 'rcw' in current implementation.
	 * Otherwise, check whether resync is now happening or should start.
	 * If yes, then the array is dirty (after unclean shutdown or
	 * initial creation), so parity in some stripes might be inconsistent.
	 * In this case, we need to always do reconstruct-write, to ensure
	 * that in case of drive failure or read-error correction, we
	 * generate correct data from the parity.
	 */
	if (conf->max_degraded == 2 ||
	    (recovery_cp < MaxSector && sh->sector >= recovery_cp)) {
		/* Calculate the real rcw later - for now make it
		 * look like rcw is cheaper
		 */
		rcw = 1; rmw = 2;
		pr_debug("force RCW max_degraded=%u, recovery_cp=%llu sh->sector=%llu\n",
			 conf->max_degraded, (unsigned long long)recovery_cp,
			 (unsigned long long)sh->sector);
	} else for (i = disks; i--; ) {
		/* would I have to read this buffer for read_modify_write */
		struct r5dev *dev = &sh->dev[i];
		if ((dev->towrite || i == sh->pd_idx) &&
		    !test_bit(R5_LOCKED, &dev->flags) &&
		    !(test_bit(R5_UPTODATE, &dev->flags) ||
		      test_bit(R5_Wantcompute, &dev->flags))) {
			if (test_bit(R5_Insync, &dev->flags))
				rmw++;
			else
				rmw += 2*disks;  /* cannot read it */
		}
		/* Would I have to read this buffer for reconstruct_write */
		if (!test_bit(R5_OVERWRITE, &dev->flags) && i != sh->pd_idx &&
		    !test_bit(R5_LOCKED, &dev->flags) &&
		    !(test_bit(R5_UPTODATE, &dev->flags) ||
		    test_bit(R5_Wantcompute, &dev->flags))) {
			if (test_bit(R5_Insync, &dev->flags)) rcw++;
			else
				rcw += 2*disks;
		}
	}
	pr_debug("for sector %llu, rmw=%d rcw=%d\n",
		(unsigned long long)sh->sector, rmw, rcw);
	set_bit(STRIPE_HANDLE, &sh->state);
	if (rmw < rcw && rmw > 0) {
		/* prefer read-modify-write, but need to get some data */
		if (conf->mddev->queue)
			blk_add_trace_msg(conf->mddev->queue,
					  "raid5 rmw %llu %d",
					  (unsigned long long)sh->sector, rmw);
		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if ((dev->towrite || i == sh->pd_idx) &&
			    !test_bit(R5_LOCKED, &dev->flags) &&
			    !(test_bit(R5_UPTODATE, &dev->flags) ||
			    test_bit(R5_Wantcompute, &dev->flags)) &&
			    test_bit(R5_Insync, &dev->flags)) {
				if (
				  test_bit(STRIPE_PREREAD_ACTIVE, &sh->state)) {
					pr_debug("Read_old block "
						 "%d for r-m-w\n", i);
					set_bit(R5_LOCKED, &dev->flags);
					set_bit(R5_Wantread, &dev->flags);
					s->locked++;
				} else {
					set_bit(STRIPE_DELAYED, &sh->state);
					set_bit(STRIPE_HANDLE, &sh->state);
				}
			}
		}
	}
	if (rcw <= rmw && rcw > 0) {
		/* want reconstruct write, but need to get some data */
		int qread =0;
		rcw = 0;
		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if (!test_bit(R5_OVERWRITE, &dev->flags) &&
			    i != sh->pd_idx && i != sh->qd_idx &&
			    !test_bit(R5_LOCKED, &dev->flags) &&
			    !(test_bit(R5_UPTODATE, &dev->flags) ||
			      test_bit(R5_Wantcompute, &dev->flags))) {
				rcw++;
				if (!test_bit(R5_Insync, &dev->flags))
					continue; /* it's a failed drive */
				if (
				  test_bit(STRIPE_PREREAD_ACTIVE, &sh->state)) {
					pr_debug("Read_old block "
						"%d for Reconstruct\n", i);
					set_bit(R5_LOCKED, &dev->flags);
					set_bit(R5_Wantread, &dev->flags);
					s->locked++;
					qread++;
				} else {
					set_bit(STRIPE_DELAYED, &sh->state);
					set_bit(STRIPE_HANDLE, &sh->state);
				}
			}
		}
		if (rcw && conf->mddev->queue)
			blk_add_trace_msg(conf->mddev->queue, "raid5 rcw %llu %d %d %d",
					  (unsigned long long)sh->sector,
					  rcw, qread, test_bit(STRIPE_DELAYED, &sh->state));
	}
	/* now if nothing is locked, and if we have enough data,
	 * we can start a write request
	 */
	/* since handle_stripe can be called at any time we need to handle the
	 * case where a compute block operation has been submitted and then a
	 * subsequent call wants to start a write request.  raid_run_ops only
	 * handles the case where compute block and reconstruct are requested
	 * simultaneously.  If this is not the case then new writes need to be
	 * held off until the compute completes.
	 */
	if ((s->req_compute || !test_bit(STRIPE_COMPUTE_RUN, &sh->state)) &&
	    (s->locked == 0 && (rcw == 0 || rmw == 0) &&
	    !test_bit(STRIPE_BIT_DELAY, &sh->state)))
		schedule_reconstruction(sh, s, rcw == 0, 0);
}

static void handle_parity_checks5(struct r5conf *conf, struct stripe_head *sh,
				struct stripe_head_state *s, int disks)
{
	struct r5dev *dev = NULL;

	set_bit(STRIPE_HANDLE, &sh->state);

	switch (sh->check_state) {
	case check_state_idle:
		/* start a new check operation if there are no failures */
		if (s->failed == 0) {
			BUG_ON(s->uptodate != disks);
			sh->check_state = check_state_run;
			set_bit(STRIPE_OP_CHECK, &s->ops_request);
			clear_bit(R5_UPTODATE, &sh->dev[sh->pd_idx].flags);
			s->uptodate--;
			break;
		}
		dev = &sh->dev[s->failed_num[0]];
		/* fall through */
	case check_state_compute_result:
		sh->check_state = check_state_idle;
		if (!dev)
			dev = &sh->dev[sh->pd_idx];

		/* check that a write has not made the stripe insync */
		if (test_bit(STRIPE_INSYNC, &sh->state))
			break;

		/* either failed parity check, or recovery is happening */
		BUG_ON(!test_bit(R5_UPTODATE, &dev->flags));
		BUG_ON(s->uptodate != disks);

		set_bit(R5_LOCKED, &dev->flags);
		s->locked++;
		set_bit(R5_Wantwrite, &dev->flags);

		clear_bit(STRIPE_DEGRADED, &sh->state);
		set_bit(STRIPE_INSYNC, &sh->state);
		break;
	case check_state_run:
		break; /* we will be called again upon completion */
	case check_state_check_result:
		sh->check_state = check_state_idle;

		/* if a failure occurred during the check operation, leave
		 * STRIPE_INSYNC not set and let the stripe be handled again
		 */
		if (s->failed)
			break;

		/* handle a successful check operation, if parity is correct
		 * we are done.  Otherwise update the mismatch count and repair
		 * parity if !MD_RECOVERY_CHECK
		 */
		if ((sh->ops.zero_sum_result & SUM_CHECK_P_RESULT) == 0)
			/* parity is correct (on disc,
			 * not in buffer any more)
			 */
			set_bit(STRIPE_INSYNC, &sh->state);
		else {
			atomic64_add(STRIPE_SECTORS, &conf->mddev->resync_mismatches);
			if (test_bit(MD_RECOVERY_CHECK, &conf->mddev->recovery))
				/* don't try to repair!! */
				set_bit(STRIPE_INSYNC, &sh->state);
			else {
				sh->check_state = check_state_compute_run;
				set_bit(STRIPE_COMPUTE_RUN, &sh->state);
				set_bit(STRIPE_OP_COMPUTE_BLK, &s->ops_request);
				set_bit(R5_Wantcompute,
					&sh->dev[sh->pd_idx].flags);
				sh->ops.target = sh->pd_idx;
				sh->ops.target2 = -1;
				s->uptodate++;
			}
		}
		break;
	case check_state_compute_run:
		break;
	default:
		printk(KERN_ERR "%s: unknown check_state: %d sector: %llu\n",
		       __func__, sh->check_state,
		       (unsigned long long) sh->sector);
		BUG();
	}
}


static void handle_parity_checks6(struct r5conf *conf, struct stripe_head *sh,
				  struct stripe_head_state *s,
				  int disks)
{
	int pd_idx = sh->pd_idx;
	int qd_idx = sh->qd_idx;
	struct r5dev *dev;

	set_bit(STRIPE_HANDLE, &sh->state);

	BUG_ON(s->failed > 2);

	/* Want to check and possibly repair P and Q.
	 * However there could be one 'failed' device, in which
	 * case we can only check one of them, possibly using the
	 * other to generate missing data
	 */

	switch (sh->check_state) {
	case check_state_idle:
		/* start a new check operation if there are < 2 failures */
		if (s->failed == s->q_failed) {
			/* The only possible failed device holds Q, so it
			 * makes sense to check P (If anything else were failed,
			 * we would have used P to recreate it).
			 */
			sh->check_state = check_state_run;
		}
		if (!s->q_failed && s->failed < 2) {
			/* Q is not failed, and we didn't use it to generate
			 * anything, so it makes sense to check it
			 */
			if (sh->check_state == check_state_run)
				sh->check_state = check_state_run_pq;
			else
				sh->check_state = check_state_run_q;
		}

		/* discard potentially stale zero_sum_result */
		sh->ops.zero_sum_result = 0;

		if (sh->check_state == check_state_run) {
			/* async_xor_zero_sum destroys the contents of P */
			clear_bit(R5_UPTODATE, &sh->dev[pd_idx].flags);
			s->uptodate--;
		}
		if (sh->check_state >= check_state_run &&
		    sh->check_state <= check_state_run_pq) {
			/* async_syndrome_zero_sum preserves P and Q, so
			 * no need to mark them !uptodate here
			 */
			set_bit(STRIPE_OP_CHECK, &s->ops_request);
			break;
		}

		/* we have 2-disk failure */
		BUG_ON(s->failed != 2);
		/* fall through */
	case check_state_compute_result:
		sh->check_state = check_state_idle;

		/* check that a write has not made the stripe insync */
		if (test_bit(STRIPE_INSYNC, &sh->state))
			break;

		/* now write out any block on a failed drive,
		 * or P or Q if they were recomputed
		 */
		BUG_ON(s->uptodate < disks - 1); /* We don't need Q to recover */
		if (s->failed == 2) {
			dev = &sh->dev[s->failed_num[1]];
			s->locked++;
			set_bit(R5_LOCKED, &dev->flags);
			set_bit(R5_Wantwrite, &dev->flags);
		}
		if (s->failed >= 1) {
			dev = &sh->dev[s->failed_num[0]];
			s->locked++;
			set_bit(R5_LOCKED, &dev->flags);
			set_bit(R5_Wantwrite, &dev->flags);
		}
		if (sh->ops.zero_sum_result & SUM_CHECK_P_RESULT) {
			dev = &sh->dev[pd_idx];
			s->locked++;
			set_bit(R5_LOCKED, &dev->flags);
			set_bit(R5_Wantwrite, &dev->flags);
		}
		if (sh->ops.zero_sum_result & SUM_CHECK_Q_RESULT) {
			dev = &sh->dev[qd_idx];
			s->locked++;
			set_bit(R5_LOCKED, &dev->flags);
			set_bit(R5_Wantwrite, &dev->flags);
		}
		clear_bit(STRIPE_DEGRADED, &sh->state);

		set_bit(STRIPE_INSYNC, &sh->state);
		break;
	case check_state_run:
	case check_state_run_q:
	case check_state_run_pq:
		break; /* we will be called again upon completion */
	case check_state_check_result:
		sh->check_state = check_state_idle;

		/* handle a successful check operation, if parity is correct
		 * we are done.  Otherwise update the mismatch count and repair
		 * parity if !MD_RECOVERY_CHECK
		 */
		if (sh->ops.zero_sum_result == 0) {
			/* both parities are correct */
			if (!s->failed)
				set_bit(STRIPE_INSYNC, &sh->state);
			else {
				/* in contrast to the raid5 case we can validate
				 * parity, but still have a failure to write
				 * back
				 */
				sh->check_state = check_state_compute_result;
				/* Returning at this point means that we may go
				 * off and bring p and/or q uptodate again so
				 * we make sure to check zero_sum_result again
				 * to verify if p or q need writeback
				 */
			}
		} else {
			atomic64_add(STRIPE_SECTORS, &conf->mddev->resync_mismatches);
			if (test_bit(MD_RECOVERY_CHECK, &conf->mddev->recovery))
				/* don't try to repair!! */
				set_bit(STRIPE_INSYNC, &sh->state);
			else {
				int *target = &sh->ops.target;

				sh->ops.target = -1;
				sh->ops.target2 = -1;
				sh->check_state = check_state_compute_run;
				set_bit(STRIPE_COMPUTE_RUN, &sh->state);
				set_bit(STRIPE_OP_COMPUTE_BLK, &s->ops_request);
				if (sh->ops.zero_sum_result & SUM_CHECK_P_RESULT) {
					set_bit(R5_Wantcompute,
						&sh->dev[pd_idx].flags);
					*target = pd_idx;
					target = &sh->ops.target2;
					s->uptodate++;
				}
				if (sh->ops.zero_sum_result & SUM_CHECK_Q_RESULT) {
					set_bit(R5_Wantcompute,
						&sh->dev[qd_idx].flags);
					*target = qd_idx;
					s->uptodate++;
				}
			}
		}
		break;
	case check_state_compute_run:
		break;
	default:
		printk(KERN_ERR "%s: unknown check_state: %d sector: %llu\n",
		       __func__, sh->check_state,
		       (unsigned long long) sh->sector);
		BUG();
	}
}

static void handle_stripe_expansion(struct r5conf *conf, struct stripe_head *sh)
{
	int i;

	/* We have read all the blocks in this stripe and now we need to
	 * copy some of them into a target stripe for expand.
	 */
	struct dma_async_tx_descriptor *tx = NULL;
	clear_bit(STRIPE_EXPAND_SOURCE, &sh->state);
	for (i = 0; i < sh->disks; i++)
		if (i != sh->pd_idx && i != sh->qd_idx) {
			int dd_idx, j;
			struct stripe_head *sh2;
			struct async_submit_ctl submit;

			sector_t bn = compute_blocknr(sh, i, 1);
			sector_t s = raid5_compute_sector(conf, bn, 0,
							  &dd_idx, NULL);
			sh2 = get_active_stripe(conf, s, 0, 1, 1);
			if (sh2 == NULL)
				/* so far only the early blocks of this stripe
				 * have been requested.  When later blocks
				 * get requested, we will try again
				 */
				continue;
			if (!test_bit(STRIPE_EXPANDING, &sh2->state) ||
			   test_bit(R5_Expanded, &sh2->dev[dd_idx].flags)) {
				/* must have already done this block */
				release_stripe(sh2);
				continue;
			}

			/* place all the copies on one channel */
			init_async_submit(&submit, 0, tx, NULL, NULL, NULL);
			tx = async_memcpy(sh2->dev[dd_idx].page,
					  sh->dev[i].page, 0, 0, STRIPE_SIZE,
					  &submit);

			set_bit(R5_Expanded, &sh2->dev[dd_idx].flags);
			set_bit(R5_UPTODATE, &sh2->dev[dd_idx].flags);
			for (j = 0; j < conf->raid_disks; j++)
				if (j != sh2->pd_idx &&
				    j != sh2->qd_idx &&
				    !test_bit(R5_Expanded, &sh2->dev[j].flags))
					break;
			if (j == conf->raid_disks) {
				set_bit(STRIPE_EXPAND_READY, &sh2->state);
				set_bit(STRIPE_HANDLE, &sh2->state);
			}
			release_stripe(sh2);

		}
	/* done submitting copies, wait for them to complete */
	async_tx_quiesce(&tx);
}

/*
 * handle_stripe - do things to a stripe.
 *
 * We lock the stripe by setting STRIPE_ACTIVE and then examine the
 * state of various bits to see what needs to be done.
 * Possible results:
 *    return some read requests which now have data
 *    return some write requests which are safely on storage
 *    schedule a read on some buffers
 *    schedule a write of some buffers
 *    return confirmation of parity correctness
 *
 */

static void analyse_stripe(struct stripe_head *sh, struct stripe_head_state *s)
{
	struct r5conf *conf = sh->raid_conf;
	int disks = sh->disks;
	struct r5dev *dev;
	int i;
	int do_recovery = 0;

	memset(s, 0, sizeof(*s));

	s->expanding = test_bit(STRIPE_EXPAND_SOURCE, &sh->state);
	s->expanded = test_bit(STRIPE_EXPAND_READY, &sh->state);
	s->failed_num[0] = -1;
	s->failed_num[1] = -1;

	/* Now to look around and see what can be done */
	rcu_read_lock();
	for (i=disks; i--; ) {
		struct md_rdev *rdev;
		sector_t first_bad;
		int bad_sectors;
		int is_bad = 0;

		dev = &sh->dev[i];

		pr_debug("check %d: state 0x%lx read %p write %p written %p\n",
			 i, dev->flags,
			 dev->toread, dev->towrite, dev->written);
		/* maybe we can reply to a read
		 *
		 * new wantfill requests are only permitted while
		 * ops_complete_biofill is guaranteed to be inactive
		 */
		if (test_bit(R5_UPTODATE, &dev->flags) && dev->toread &&
		    !test_bit(STRIPE_BIOFILL_RUN, &sh->state))
			set_bit(R5_Wantfill, &dev->flags);

		/* now count some things */
		if (test_bit(R5_LOCKED, &dev->flags))
			s->locked++;
		if (test_bit(R5_UPTODATE, &dev->flags))
			s->uptodate++;
		if (test_bit(R5_Wantcompute, &dev->flags)) {
			s->compute++;
			BUG_ON(s->compute > 2);
		}

		if (test_bit(R5_Wantfill, &dev->flags))
			s->to_fill++;
		else if (dev->toread)
			s->to_read++;
		if (dev->towrite) {
			s->to_write++;
			if (!test_bit(R5_OVERWRITE, &dev->flags))
				s->non_overwrite++;
		}
		if (dev->written)
			s->written++;
		/* Prefer to use the replacement for reads, but only
		 * if it is recovered enough and has no bad blocks.
		 */
		rdev = rcu_dereference(conf->disks[i].replacement);
		if (rdev && !test_bit(Faulty, &rdev->flags) &&
		    rdev->recovery_offset >= sh->sector + STRIPE_SECTORS &&
		    !is_badblock(rdev, sh->sector, STRIPE_SECTORS,
				 &first_bad, &bad_sectors))
			set_bit(R5_ReadRepl, &dev->flags);
		else {
			if (rdev)
				set_bit(R5_NeedReplace, &dev->flags);
			rdev = rcu_dereference(conf->disks[i].rdev);
			clear_bit(R5_ReadRepl, &dev->flags);
		}
		if (rdev && test_bit(Faulty, &rdev->flags))
			rdev = NULL;
		if (rdev) {
			is_bad = is_badblock(rdev, sh->sector, STRIPE_SECTORS,
					     &first_bad, &bad_sectors);
			if (s->blocked_rdev == NULL
			    && (test_bit(Blocked, &rdev->flags)
				|| is_bad < 0)) {
				if (is_bad < 0)
					set_bit(BlockedBadBlocks,
						&rdev->flags);
				s->blocked_rdev = rdev;
				atomic_inc(&rdev->nr_pending);
			}
		}
		clear_bit(R5_Insync, &dev->flags);
		if (!rdev)
			/* Not in-sync */;
		else if (is_bad) {
			/* also not in-sync */
			if (!test_bit(WriteErrorSeen, &rdev->flags) &&
			    test_bit(R5_UPTODATE, &dev->flags)) {
				/* treat as in-sync, but with a read error
				 * which we can now try to correct
				 */
				set_bit(R5_Insync, &dev->flags);
				set_bit(R5_ReadError, &dev->flags);
			}
		} else if (test_bit(In_sync, &rdev->flags))
			set_bit(R5_Insync, &dev->flags);
		else if (sh->sector + STRIPE_SECTORS <= rdev->recovery_offset)
			/* in sync if before recovery_offset */
			set_bit(R5_Insync, &dev->flags);
		else if (test_bit(R5_UPTODATE, &dev->flags) &&
			 test_bit(R5_Expanded, &dev->flags))
			/* If we've reshaped into here, we assume it is Insync.
			 * We will shortly update recovery_offset to make
			 * it official.
			 */
			set_bit(R5_Insync, &dev->flags);

		if (rdev && test_bit(R5_WriteError, &dev->flags)) {
			/* This flag does not apply to '.replacement'
			 * only to .rdev, so make sure to check that*/
			struct md_rdev *rdev2 = rcu_dereference(
				conf->disks[i].rdev);
			if (rdev2 == rdev)
				clear_bit(R5_Insync, &dev->flags);
			if (rdev2 && !test_bit(Faulty, &rdev2->flags)) {
				s->handle_bad_blocks = 1;
				atomic_inc(&rdev2->nr_pending);
			} else
				clear_bit(R5_WriteError, &dev->flags);
		}
		if (rdev && test_bit(R5_MadeGood, &dev->flags)) {
			/* This flag does not apply to '.replacement'
			 * only to .rdev, so make sure to check that*/
			struct md_rdev *rdev2 = rcu_dereference(
				conf->disks[i].rdev);
			if (rdev2 && !test_bit(Faulty, &rdev2->flags)) {
				s->handle_bad_blocks = 1;
				atomic_inc(&rdev2->nr_pending);
			} else
				clear_bit(R5_MadeGood, &dev->flags);
		}
		if (test_bit(R5_MadeGoodRepl, &dev->flags)) {
			struct md_rdev *rdev2 = rcu_dereference(
				conf->disks[i].replacement);
			if (rdev2 && !test_bit(Faulty, &rdev2->flags)) {
				s->handle_bad_blocks = 1;
				atomic_inc(&rdev2->nr_pending);
			} else
				clear_bit(R5_MadeGoodRepl, &dev->flags);
		}
		if (!test_bit(R5_Insync, &dev->flags)) {
			/* The ReadError flag will just be confusing now */
			clear_bit(R5_ReadError, &dev->flags);
			clear_bit(R5_ReWrite, &dev->flags);
		}
		if (test_bit(R5_ReadError, &dev->flags))
			clear_bit(R5_Insync, &dev->flags);
		if (!test_bit(R5_Insync, &dev->flags)) {
			if (s->failed < 2)
				s->failed_num[s->failed] = i;
			s->failed++;
			if (rdev && !test_bit(Faulty, &rdev->flags))
				do_recovery = 1;
		}
	}
	if (test_bit(STRIPE_SYNCING, &sh->state)) {
		/* If there is a failed device being replaced,
		 *     we must be recovering.
		 * else if we are after recovery_cp, we must be syncing
		 * else if MD_RECOVERY_REQUESTED is set, we also are syncing.
		 * else we can only be replacing
		 * sync and recovery both need to read all devices, and so
		 * use the same flag.
		 */
		if (do_recovery ||
		    sh->sector >= conf->mddev->recovery_cp ||
		    test_bit(MD_RECOVERY_REQUESTED, &(conf->mddev->recovery)))
			s->syncing = 1;
		else
			s->replacing = 1;
	}
	rcu_read_unlock();
}

static void handle_stripe(struct stripe_head *sh)
{
	struct stripe_head_state s;
	struct r5conf *conf = sh->raid_conf;
	int i;
	int prexor;
	int disks = sh->disks;
	struct r5dev *pdev, *qdev;

	clear_bit(STRIPE_HANDLE, &sh->state);
	if (test_and_set_bit_lock(STRIPE_ACTIVE, &sh->state)) {
		/* already being handled, ensure it gets handled
		 * again when current action finishes */
		set_bit(STRIPE_HANDLE, &sh->state);
		return;
	}

	if (test_bit(STRIPE_SYNC_REQUESTED, &sh->state)) {
		spin_lock(&sh->stripe_lock);
		/* Cannot process 'sync' concurrently with 'discard' */
		if (!test_bit(STRIPE_DISCARD, &sh->state) &&
		    test_and_clear_bit(STRIPE_SYNC_REQUESTED, &sh->state)) {
			set_bit(STRIPE_SYNCING, &sh->state);
			clear_bit(STRIPE_INSYNC, &sh->state);
			clear_bit(STRIPE_REPLACED, &sh->state);
		}
		spin_unlock(&sh->stripe_lock);
	}
	clear_bit(STRIPE_DELAYED, &sh->state);

	pr_debug("handling stripe %llu, state=%#lx cnt=%d, "
		"pd_idx=%d, qd_idx=%d\n, check:%d, reconstruct:%d\n",
	       (unsigned long long)sh->sector, sh->state,
	       atomic_read(&sh->count), sh->pd_idx, sh->qd_idx,
	       sh->check_state, sh->reconstruct_state);

	analyse_stripe(sh, &s);

	if (s.handle_bad_blocks) {
		set_bit(STRIPE_HANDLE, &sh->state);
		goto finish;
	}

	if (unlikely(s.blocked_rdev)) {
		if (s.syncing || s.expanding || s.expanded ||
		    s.replacing || s.to_write || s.written) {
			set_bit(STRIPE_HANDLE, &sh->state);
			goto finish;
		}
		/* There is nothing for the blocked_rdev to block */
		rdev_dec_pending(s.blocked_rdev, conf->mddev);
		s.blocked_rdev = NULL;
	}

	if (s.to_fill && !test_bit(STRIPE_BIOFILL_RUN, &sh->state)) {
		set_bit(STRIPE_OP_BIOFILL, &s.ops_request);
		set_bit(STRIPE_BIOFILL_RUN, &sh->state);
	}

	pr_debug("locked=%d uptodate=%d to_read=%d"
	       " to_write=%d failed=%d failed_num=%d,%d\n",
	       s.locked, s.uptodate, s.to_read, s.to_write, s.failed,
	       s.failed_num[0], s.failed_num[1]);
	/* check if the array has lost more than max_degraded devices and,
	 * if so, some requests might need to be failed.
	 */
	if (s.failed > conf->max_degraded) {
		sh->check_state = 0;
		sh->reconstruct_state = 0;
		if (s.to_read+s.to_write+s.written)
			handle_failed_stripe(conf, sh, &s, disks, &s.return_bi);
		if (s.syncing + s.replacing)
			handle_failed_sync(conf, sh, &s);
	}

	/* Now we check to see if any write operations have recently
	 * completed
	 */
	prexor = 0;
	if (sh->reconstruct_state == reconstruct_state_prexor_drain_result)
		prexor = 1;
	if (sh->reconstruct_state == reconstruct_state_drain_result ||
	    sh->reconstruct_state == reconstruct_state_prexor_drain_result) {
		sh->reconstruct_state = reconstruct_state_idle;

		/* All the 'written' buffers and the parity block are ready to
		 * be written back to disk
		 */
		BUG_ON(!test_bit(R5_UPTODATE, &sh->dev[sh->pd_idx].flags) &&
		       !test_bit(R5_Discard, &sh->dev[sh->pd_idx].flags));
		BUG_ON(sh->qd_idx >= 0 &&
		       !test_bit(R5_UPTODATE, &sh->dev[sh->qd_idx].flags) &&
		       !test_bit(R5_Discard, &sh->dev[sh->qd_idx].flags));
		for (i = disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			if (test_bit(R5_LOCKED, &dev->flags) &&
				(i == sh->pd_idx || i == sh->qd_idx ||
				 dev->written)) {
				pr_debug("Writing block %d\n", i);
				set_bit(R5_Wantwrite, &dev->flags);
				if (prexor)
					continue;
				if (!test_bit(R5_Insync, &dev->flags) ||
				    ((i == sh->pd_idx || i == sh->qd_idx)  &&
				     s.failed == 0))
					set_bit(STRIPE_INSYNC, &sh->state);
			}
		}
		if (test_and_clear_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
			s.dec_preread_active = 1;
	}

	/*
	 * might be able to return some write requests if the parity blocks
	 * are safe, or on a failed drive
	 */
	pdev = &sh->dev[sh->pd_idx];
	s.p_failed = (s.failed >= 1 && s.failed_num[0] == sh->pd_idx)
		|| (s.failed >= 2 && s.failed_num[1] == sh->pd_idx);
	qdev = &sh->dev[sh->qd_idx];
	s.q_failed = (s.failed >= 1 && s.failed_num[0] == sh->qd_idx)
		|| (s.failed >= 2 && s.failed_num[1] == sh->qd_idx)
		|| conf->level < 6;

	if (s.written &&
	    (s.p_failed || ((test_bit(R5_Insync, &pdev->flags)
			     && !test_bit(R5_LOCKED, &pdev->flags)
			     && (test_bit(R5_UPTODATE, &pdev->flags) ||
				 test_bit(R5_Discard, &pdev->flags))))) &&
	    (s.q_failed || ((test_bit(R5_Insync, &qdev->flags)
			     && !test_bit(R5_LOCKED, &qdev->flags)
			     && (test_bit(R5_UPTODATE, &qdev->flags) ||
				 test_bit(R5_Discard, &qdev->flags))))))
		handle_stripe_clean_event(conf, sh, disks, &s.return_bi);

	/* Now we might consider reading some blocks, either to check/generate
	 * parity, or to satisfy requests
	 * or to load a block that is being partially written.
	 */
	if (s.to_read || s.non_overwrite
	    || (conf->level == 6 && s.to_write && s.failed)
	    || (s.syncing && (s.uptodate + s.compute < disks))
	    || s.replacing
	    || s.expanding)
		handle_stripe_fill(sh, &s, disks);

	/* Now to consider new write requests and what else, if anything
	 * should be read.  We do not handle new writes when:
	 * 1/ A 'write' operation (copy+xor) is already in flight.
	 * 2/ A 'check' operation is in flight, as it may clobber the parity
	 *    block.
	 */
	if (s.to_write && !sh->reconstruct_state && !sh->check_state)
		handle_stripe_dirtying(conf, sh, &s, disks);

	/* maybe we need to check and possibly fix the parity for this stripe
	 * Any reads will already have been scheduled, so we just see if enough
	 * data is available.  The parity check is held off while parity
	 * dependent operations are in flight.
	 */
	if (sh->check_state ||
	    (s.syncing && s.locked == 0 &&
	     !test_bit(STRIPE_COMPUTE_RUN, &sh->state) &&
	     !test_bit(STRIPE_INSYNC, &sh->state))) {
		if (conf->level == 6)
			handle_parity_checks6(conf, sh, &s, disks);
		else
			handle_parity_checks5(conf, sh, &s, disks);
	}

	if ((s.replacing || s.syncing) && s.locked == 0
	    && !test_bit(STRIPE_COMPUTE_RUN, &sh->state)
	    && !test_bit(STRIPE_REPLACED, &sh->state)) {
		/* Write out to replacement devices where possible */
		for (i = 0; i < conf->raid_disks; i++)
			if (test_bit(R5_NeedReplace, &sh->dev[i].flags)) {
				WARN_ON(!test_bit(R5_UPTODATE, &sh->dev[i].flags));
				set_bit(R5_WantReplace, &sh->dev[i].flags);
				set_bit(R5_LOCKED, &sh->dev[i].flags);
				s.locked++;
			}
		if (s.replacing)
			set_bit(STRIPE_INSYNC, &sh->state);
		set_bit(STRIPE_REPLACED, &sh->state);
	}
	if ((s.syncing || s.replacing) && s.locked == 0 &&
	    !test_bit(STRIPE_COMPUTE_RUN, &sh->state) &&
	    test_bit(STRIPE_INSYNC, &sh->state)) {
		md_done_sync(conf->mddev, STRIPE_SECTORS, 1);
		clear_bit(STRIPE_SYNCING, &sh->state);
		if (test_and_clear_bit(R5_Overlap, &sh->dev[sh->pd_idx].flags))
			wake_up(&conf->wait_for_overlap);
	}

	/* If the failed drives are just a ReadError, then we might need
	 * to progress the repair/check process
	 */
	if (s.failed <= conf->max_degraded && !conf->mddev->ro)
		for (i = 0; i < s.failed; i++) {
			struct r5dev *dev = &sh->dev[s.failed_num[i]];
			if (test_bit(R5_ReadError, &dev->flags)
			    && !test_bit(R5_LOCKED, &dev->flags)
			    && test_bit(R5_UPTODATE, &dev->flags)
				) {
				if (!test_bit(R5_ReWrite, &dev->flags)) {
					set_bit(R5_Wantwrite, &dev->flags);
					set_bit(R5_ReWrite, &dev->flags);
					set_bit(R5_LOCKED, &dev->flags);
					s.locked++;
				} else {
					/* let's read it back */
					set_bit(R5_Wantread, &dev->flags);
					set_bit(R5_LOCKED, &dev->flags);
					s.locked++;
				}
			}
		}


	/* Finish reconstruct operations initiated by the expansion process */
	if (sh->reconstruct_state == reconstruct_state_result) {
		struct stripe_head *sh_src
			= get_active_stripe(conf, sh->sector, 1, 1, 1);
		if (sh_src && test_bit(STRIPE_EXPAND_SOURCE, &sh_src->state)) {
			/* sh cannot be written until sh_src has been read.
			 * so arrange for sh to be delayed a little
			 */
			set_bit(STRIPE_DELAYED, &sh->state);
			set_bit(STRIPE_HANDLE, &sh->state);
			if (!test_and_set_bit(STRIPE_PREREAD_ACTIVE,
					      &sh_src->state))
				atomic_inc(&conf->preread_active_stripes);
			release_stripe(sh_src);
			goto finish;
		}
		if (sh_src)
			release_stripe(sh_src);

		sh->reconstruct_state = reconstruct_state_idle;
		clear_bit(STRIPE_EXPANDING, &sh->state);
		for (i = conf->raid_disks; i--; ) {
			set_bit(R5_Wantwrite, &sh->dev[i].flags);
			set_bit(R5_LOCKED, &sh->dev[i].flags);
			s.locked++;
		}
	}

	if (s.expanded && test_bit(STRIPE_EXPANDING, &sh->state) &&
	    !sh->reconstruct_state) {
		/* Need to write out all blocks after computing parity */
		sh->disks = conf->raid_disks;
		stripe_set_idx(sh->sector, conf, 0, sh);
		schedule_reconstruction(sh, &s, 1, 1);
	} else if (s.expanded && !sh->reconstruct_state && s.locked == 0) {
		clear_bit(STRIPE_EXPAND_READY, &sh->state);
		atomic_dec(&conf->reshape_stripes);
		wake_up(&conf->wait_for_overlap);
		md_done_sync(conf->mddev, STRIPE_SECTORS, 1);
	}

	if (s.expanding && s.locked == 0 &&
	    !test_bit(STRIPE_COMPUTE_RUN, &sh->state))
		handle_stripe_expansion(conf, sh);

finish:
	/* wait for this device to become unblocked */
	if (unlikely(s.blocked_rdev)) {
		if (conf->mddev->external)
			md_wait_for_blocked_rdev(s.blocked_rdev,
						 conf->mddev);
		else
			/* Internal metadata will immediately
			 * be written by raid5d, so we don't
			 * need to wait here.
			 */
			rdev_dec_pending(s.blocked_rdev,
					 conf->mddev);
	}

	if (s.handle_bad_blocks)
		for (i = disks; i--; ) {
			struct md_rdev *rdev;
			struct r5dev *dev = &sh->dev[i];
			if (test_and_clear_bit(R5_WriteError, &dev->flags)) {
				/* We own a safe reference to the rdev */
				rdev = conf->disks[i].rdev;
				if (!rdev_set_badblocks(rdev, sh->sector,
							STRIPE_SECTORS, 0))
					md_error(conf->mddev, rdev);
				rdev_dec_pending(rdev, conf->mddev);
			}
			if (test_and_clear_bit(R5_MadeGood, &dev->flags)) {
				rdev = conf->disks[i].rdev;
				rdev_clear_badblocks(rdev, sh->sector,
						     STRIPE_SECTORS, 0);
				rdev_dec_pending(rdev, conf->mddev);
			}
			if (test_and_clear_bit(R5_MadeGoodRepl, &dev->flags)) {
				rdev = conf->disks[i].replacement;
				if (!rdev)
					/* rdev have been moved down */
					rdev = conf->disks[i].rdev;
				rdev_clear_badblocks(rdev, sh->sector,
						     STRIPE_SECTORS, 0);
				rdev_dec_pending(rdev, conf->mddev);
			}
		}

	if (s.ops_request)
		raid_run_ops(sh, s.ops_request);

	ops_run_io(sh, &s);

	if (s.dec_preread_active) {
		/* We delay this until after ops_run_io so that if make_request
		 * is waiting on a flush, it won't continue until the writes
		 * have actually been submitted.
		 */
		atomic_dec(&conf->preread_active_stripes);
		if (atomic_read(&conf->preread_active_stripes) <
		    IO_THRESHOLD)
			md_wakeup_thread(conf->mddev->thread);
	}

	return_io(s.return_bi);

	clear_bit_unlock(STRIPE_ACTIVE, &sh->state);
}

static void raid5_activate_delayed(struct r5conf *conf)
{
	if (atomic_read(&conf->preread_active_stripes) < IO_THRESHOLD) {
		while (!list_empty(&conf->delayed_list)) {
			struct list_head *l = conf->delayed_list.next;
			struct stripe_head *sh;
			sh = list_entry(l, struct stripe_head, lru);
			list_del_init(l);
			clear_bit(STRIPE_DELAYED, &sh->state);
			if (!test_and_set_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
				atomic_inc(&conf->preread_active_stripes);
			list_add_tail(&sh->lru, &conf->hold_list);
			raid5_wakeup_stripe_thread(sh);
		}
	}
}

static void activate_bit_delay(struct r5conf *conf)
{
	/* device_lock is held */
	struct list_head head;
	list_add(&head, &conf->bitmap_list);
	list_del_init(&conf->bitmap_list);
	while (!list_empty(&head)) {
		struct stripe_head *sh = list_entry(head.next, struct stripe_head, lru);
		list_del_init(&sh->lru);
		atomic_inc(&sh->count);
		__release_stripe(conf, sh);
	}
}

int md_raid5_congested(struct mddev *mddev, int bits)
{
	struct r5conf *conf = mddev->private;

	/* No difference between reads and writes.  Just check
	 * how busy the stripe_cache is
	 */

	if (conf->inactive_blocked)
		return 1;
	if (conf->quiesce)
		return 1;
	if (list_empty_careful(&conf->inactive_list))
		return 1;

	return 0;
}
EXPORT_SYMBOL_GPL(md_raid5_congested);

static int raid5_congested(void *data, int bits)
{
	struct mddev *mddev = data;

	return mddev_congested(mddev, bits) ||
		md_raid5_congested(mddev, bits);
}

/* We want read requests to align with chunks where possible,
 * but write requests don't need to.
 */
static int raid5_mergeable_bvec(struct request_queue *q,
				struct bvec_merge_data *bvm,
				struct bio_vec *biovec)
{
	struct mddev *mddev = q->queuedata;
	sector_t sector = bvm->bi_sector + get_start_sect(bvm->bi_bdev);
	int max;
	unsigned int chunk_sectors = mddev->chunk_sectors;
	unsigned int bio_sectors = bvm->bi_size >> 9;

	if ((bvm->bi_rw & 1) == WRITE)
		return biovec->bv_len; /* always allow writes to be mergeable */

	if (mddev->new_chunk_sectors < mddev->chunk_sectors)
		chunk_sectors = mddev->new_chunk_sectors;
	max =  (chunk_sectors - ((sector & (chunk_sectors - 1)) + bio_sectors)) << 9;
	if (max < 0) max = 0;
	if (max <= biovec->bv_len && bio_sectors == 0)
		return biovec->bv_len;
	else
		return max;
}


static int in_chunk_boundary(struct mddev *mddev, struct bio *bio)
{
	sector_t sector = bio->bi_sector + get_start_sect(bio->bi_bdev);
	unsigned int chunk_sectors = mddev->chunk_sectors;
	unsigned int bio_sectors = bio_sectors(bio);

	if (mddev->new_chunk_sectors < mddev->chunk_sectors)
		chunk_sectors = mddev->new_chunk_sectors;
	return  chunk_sectors >=
		((sector & (chunk_sectors - 1)) + bio_sectors);
}

/*
 *  add bio to the retry LIFO  ( in O(1) ... we are in interrupt )
 *  later sampled by raid5d.
 */
static void add_bio_to_retry(struct bio *bi,struct r5conf *conf)
{
	unsigned long flags;

	spin_lock_irqsave(&conf->device_lock, flags);

	bi->bi_next = conf->retry_read_aligned_list;
	conf->retry_read_aligned_list = bi;

	spin_unlock_irqrestore(&conf->device_lock, flags);
	md_wakeup_thread(conf->mddev->thread);
}


static struct bio *remove_bio_from_retry(struct r5conf *conf)
{
	struct bio *bi;

	bi = conf->retry_read_aligned;
	if (bi) {
		conf->retry_read_aligned = NULL;
		return bi;
	}
	bi = conf->retry_read_aligned_list;
	if(bi) {
		conf->retry_read_aligned_list = bi->bi_next;
		bi->bi_next = NULL;
		/*
		 * this sets the active strip count to 1 and the processed
		 * strip count to zero (upper 8 bits)
		 */
		raid5_set_bi_stripes(bi, 1); /* biased count of active stripes */
	}

	return bi;
}


/*
 *  The "raid5_align_endio" should check if the read succeeded and if it
 *  did, call bio_endio on the original bio (having bio_put the new bio
 *  first).
 *  If the read failed..
 */
static void raid5_align_endio(struct bio *bi, int error)
{
	struct bio* raid_bi  = bi->bi_private;
	struct mddev *mddev;
	struct r5conf *conf;
	int uptodate = test_bit(BIO_UPTODATE, &bi->bi_flags);
	struct md_rdev *rdev;

	bio_put(bi);

	rdev = (void*)raid_bi->bi_next;
	raid_bi->bi_next = NULL;
	mddev = rdev->mddev;
	conf = mddev->private;

	rdev_dec_pending(rdev, conf->mddev);

	if (!error && uptodate) {
		trace_block_bio_complete(bdev_get_queue(raid_bi->bi_bdev),
					 raid_bi, 0);
		bio_endio(raid_bi, 0);
		if (atomic_dec_and_test(&conf->active_aligned_reads))
			wake_up(&conf->wait_for_stripe);
		return;
	}


	pr_debug("raid5_align_endio : io error...handing IO for a retry\n");

	add_bio_to_retry(raid_bi, conf);
}

static int bio_fits_rdev(struct bio *bi)
{
	struct request_queue *q = bdev_get_queue(bi->bi_bdev);

	if (bio_sectors(bi) > queue_max_sectors(q))
		return 0;
	blk_recount_segments(q, bi);
	if (bi->bi_phys_segments > queue_max_segments(q))
		return 0;

	if (q->merge_bvec_fn)
		/* it's too hard to apply the merge_bvec_fn at this stage,
		 * just just give up
		 */
		return 0;

	return 1;
}


static int chunk_aligned_read(struct mddev *mddev, struct bio * raid_bio)
{
	struct r5conf *conf = mddev->private;
	int dd_idx;
	struct bio* align_bi;
	struct md_rdev *rdev;
	sector_t end_sector;

	if (!in_chunk_boundary(mddev, raid_bio)) {
		pr_debug("chunk_aligned_read : non aligned\n");
		return 0;
	}
	/*
	 * use bio_clone_mddev to make a copy of the bio
	 */
	align_bi = bio_clone_mddev(raid_bio, GFP_NOIO, mddev);
	if (!align_bi)
		return 0;
	/*
	 *   set bi_end_io to a new function, and set bi_private to the
	 *     original bio.
	 */
	align_bi->bi_end_io  = raid5_align_endio;
	align_bi->bi_private = raid_bio;
	/*
	 *	compute position
	 */
	align_bi->bi_sector =  raid5_compute_sector(conf, raid_bio->bi_sector,
						    0,
						    &dd_idx, NULL);

	end_sector = bio_end_sector(align_bi);
	rcu_read_lock();
	rdev = rcu_dereference(conf->disks[dd_idx].replacement);
	if (!rdev || test_bit(Faulty, &rdev->flags) ||
	    rdev->recovery_offset < end_sector) {
		rdev = rcu_dereference(conf->disks[dd_idx].rdev);
		if (rdev &&
		    (test_bit(Faulty, &rdev->flags) ||
		    !(test_bit(In_sync, &rdev->flags) ||
		      rdev->recovery_offset >= end_sector)))
			rdev = NULL;
	}
	if (rdev) {
		sector_t first_bad;
		int bad_sectors;

		atomic_inc(&rdev->nr_pending);
		rcu_read_unlock();
		raid_bio->bi_next = (void*)rdev;
		align_bi->bi_bdev =  rdev->bdev;
		align_bi->bi_flags &= ~(1 << BIO_SEG_VALID);

		if (!bio_fits_rdev(align_bi) ||
		    is_badblock(rdev, align_bi->bi_sector, bio_sectors(align_bi),
				&first_bad, &bad_sectors)) {
			/* too big in some way, or has a known bad block */
			bio_put(align_bi);
			rdev_dec_pending(rdev, mddev);
			return 0;
		}

		/* No reshape active, so we can trust rdev->data_offset */
		align_bi->bi_sector += rdev->data_offset;

		spin_lock_irq(&conf->device_lock);
		wait_event_lock_irq(conf->wait_for_stripe,
				    conf->quiesce == 0,
				    conf->device_lock);
		atomic_inc(&conf->active_aligned_reads);
		spin_unlock_irq(&conf->device_lock);

		if (mddev->gendisk)
			trace_block_bio_remap(bdev_get_queue(align_bi->bi_bdev),
					      align_bi, disk_devt(mddev->gendisk),
					      raid_bio->bi_sector);
		generic_make_request(align_bi);
		return 1;
	} else {
		rcu_read_unlock();
		bio_put(align_bi);
		return 0;
	}
}

/* __get_priority_stripe - get the next stripe to process
 *
 * Full stripe writes are allowed to pass preread active stripes up until
 * the bypass_threshold is exceeded.  In general the bypass_count
 * increments when the handle_list is handled before the hold_list; however, it
 * will not be incremented when STRIPE_IO_STARTED is sampled set signifying a
 * stripe with in flight i/o.  The bypass_count will be reset when the
 * head of the hold_list has changed, i.e. the head was promoted to the
 * handle_list.
 */
static struct stripe_head *__get_priority_stripe(struct r5conf *conf, int group)
{
	struct stripe_head *sh = NULL, *tmp;
	struct list_head *handle_list = NULL;

	if (conf->worker_cnt_per_group == 0) {
		handle_list = &conf->handle_list;
	} else if (group != ANY_GROUP) {
		handle_list = &conf->worker_groups[group].handle_list;
	} else {
		int i;
		for (i = 0; i < conf->group_cnt; i++) {
			handle_list = &conf->worker_groups[i].handle_list;
			if (!list_empty(handle_list))
				break;
		}
	}

	pr_debug("%s: handle: %s hold: %s full_writes: %d bypass_count: %d\n",
		  __func__,
		  list_empty(handle_list) ? "empty" : "busy",
		  list_empty(&conf->hold_list) ? "empty" : "busy",
		  atomic_read(&conf->pending_full_writes), conf->bypass_count);

	if (!list_empty(handle_list)) {
		sh = list_entry(handle_list->next, typeof(*sh), lru);

		if (list_empty(&conf->hold_list))
			conf->bypass_count = 0;
		else if (!test_bit(STRIPE_IO_STARTED, &sh->state)) {
			if (conf->hold_list.next == conf->last_hold)
				conf->bypass_count++;
			else {
				conf->last_hold = conf->hold_list.next;
				conf->bypass_count -= conf->bypass_threshold;
				if (conf->bypass_count < 0)
					conf->bypass_count = 0;
			}
		}
	} else if (!list_empty(&conf->hold_list) &&
		   ((conf->bypass_threshold &&
		     conf->bypass_count > conf->bypass_threshold) ||
		    atomic_read(&conf->pending_full_writes) == 0)) {

		list_for_each_entry(tmp, &conf->hold_list,  lru) {
			if (conf->worker_cnt_per_group == 0 ||
			    group == ANY_GROUP ||
			    !cpu_online(tmp->cpu) ||
			    cpu_to_group(tmp->cpu) == group) {
				sh = tmp;
				break;
			}
		}

		if (sh) {
			conf->bypass_count -= conf->bypass_threshold;
			if (conf->bypass_count < 0)
				conf->bypass_count = 0;
		}
	}

	if (!sh)
		return NULL;

	list_del_init(&sh->lru);
	atomic_inc(&sh->count);
	BUG_ON(atomic_read(&sh->count) != 1);
	return sh;
}

struct raid5_plug_cb {
	struct blk_plug_cb	cb;
	struct list_head	list;
};

static void raid5_unplug(struct blk_plug_cb *blk_cb, bool from_schedule)
{
	struct raid5_plug_cb *cb = container_of(
		blk_cb, struct raid5_plug_cb, cb);
	struct stripe_head *sh;
	struct mddev *mddev = cb->cb.data;
	struct r5conf *conf = mddev->private;
	int cnt = 0;

	if (cb->list.next && !list_empty(&cb->list)) {
		spin_lock_irq(&conf->device_lock);
		while (!list_empty(&cb->list)) {
			sh = list_first_entry(&cb->list, struct stripe_head, lru);
			list_del_init(&sh->lru);
			/*
			 * avoid race release_stripe_plug() sees
			 * STRIPE_ON_UNPLUG_LIST clear but the stripe
			 * is still in our list
			 */
			smp_mb__before_clear_bit();
			clear_bit(STRIPE_ON_UNPLUG_LIST, &sh->state);
			/*
			 * STRIPE_ON_RELEASE_LIST could be set here. In that
			 * case, the count is always > 1 here
			 */
			__release_stripe(conf, sh);
			cnt++;
		}
		spin_unlock_irq(&conf->device_lock);
	}
	if (mddev->queue)
		trace_block_unplug(mddev->queue, cnt, !from_schedule);
	kfree(cb);
}

static void release_stripe_plug(struct mddev *mddev,
				struct stripe_head *sh)
{
	struct blk_plug_cb *blk_cb = blk_check_plugged(
		raid5_unplug, mddev,
		sizeof(struct raid5_plug_cb));
	struct raid5_plug_cb *cb;

	if (!blk_cb) {
		release_stripe(sh);
		return;
	}

	cb = container_of(blk_cb, struct raid5_plug_cb, cb);

	if (cb->list.next == NULL)
		INIT_LIST_HEAD(&cb->list);

	if (!test_and_set_bit(STRIPE_ON_UNPLUG_LIST, &sh->state))
		list_add_tail(&sh->lru, &cb->list);
	else
		release_stripe(sh);
}

static void make_discard_request(struct mddev *mddev, struct bio *bi)
{
	struct r5conf *conf = mddev->private;
	sector_t logical_sector, last_sector;
	struct stripe_head *sh;
	int remaining;
	int stripe_sectors;

	if (mddev->reshape_position != MaxSector)
		/* Skip discard while reshape is happening */
		return;

	logical_sector = bi->bi_sector & ~((sector_t)STRIPE_SECTORS-1);
	last_sector = bi->bi_sector + (bi->bi_size>>9);

	bi->bi_next = NULL;
	bi->bi_phys_segments = 1; /* over-loaded to count active stripes */

	stripe_sectors = conf->chunk_sectors *
		(conf->raid_disks - conf->max_degraded);
	logical_sector = DIV_ROUND_UP_SECTOR_T(logical_sector,
					       stripe_sectors);
	sector_div(last_sector, stripe_sectors);

	logical_sector *= conf->chunk_sectors;
	last_sector *= conf->chunk_sectors;

	for (; logical_sector < last_sector;
	     logical_sector += STRIPE_SECTORS) {
		DEFINE_WAIT(w);
		int d;
	again:
		sh = get_active_stripe(conf, logical_sector, 0, 0, 0);
		prepare_to_wait(&conf->wait_for_overlap, &w,
				TASK_UNINTERRUPTIBLE);
		set_bit(R5_Overlap, &sh->dev[sh->pd_idx].flags);
		if (test_bit(STRIPE_SYNCING, &sh->state)) {
			release_stripe(sh);
			schedule();
			goto again;
		}
		clear_bit(R5_Overlap, &sh->dev[sh->pd_idx].flags);
		spin_lock_irq(&sh->stripe_lock);
		for (d = 0; d < conf->raid_disks; d++) {
			if (d == sh->pd_idx || d == sh->qd_idx)
				continue;
			if (sh->dev[d].towrite || sh->dev[d].toread) {
				set_bit(R5_Overlap, &sh->dev[d].flags);
				spin_unlock_irq(&sh->stripe_lock);
				release_stripe(sh);
				schedule();
				goto again;
			}
		}
		set_bit(STRIPE_DISCARD, &sh->state);
		finish_wait(&conf->wait_for_overlap, &w);
		for (d = 0; d < conf->raid_disks; d++) {
			if (d == sh->pd_idx || d == sh->qd_idx)
				continue;
			sh->dev[d].towrite = bi;
			set_bit(R5_OVERWRITE, &sh->dev[d].flags);
			raid5_inc_bi_active_stripes(bi);
		}
		spin_unlock_irq(&sh->stripe_lock);
		if (conf->mddev->bitmap) {
			for (d = 0;
			     d < conf->raid_disks - conf->max_degraded;
			     d++)
				bitmap_startwrite(mddev->bitmap,
						  sh->sector,
						  STRIPE_SECTORS,
						  0);
			sh->bm_seq = conf->seq_flush + 1;
			set_bit(STRIPE_BIT_DELAY, &sh->state);
		}

		set_bit(STRIPE_HANDLE, &sh->state);
		clear_bit(STRIPE_DELAYED, &sh->state);
		if (!test_and_set_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
			atomic_inc(&conf->preread_active_stripes);
		release_stripe_plug(mddev, sh);
	}

	remaining = raid5_dec_bi_active_stripes(bi);
	if (remaining == 0) {
		md_write_end(mddev);
		bio_endio(bi, 0);
	}
}

static void make_request(struct mddev *mddev, struct bio * bi)
{
	struct r5conf *conf = mddev->private;
	int dd_idx;
	sector_t new_sector;
	sector_t logical_sector, last_sector;
	struct stripe_head *sh;
	const int rw = bio_data_dir(bi);
	int remaining;

	if (unlikely(bi->bi_rw & REQ_FLUSH)) {
		md_flush_request(mddev, bi);
		return;
	}

	md_write_start(mddev, bi);

	if (rw == READ &&
	     mddev->reshape_position == MaxSector &&
	     chunk_aligned_read(mddev,bi))
		return;

	if (unlikely(bi->bi_rw & REQ_DISCARD)) {
		make_discard_request(mddev, bi);
		return;
	}

	logical_sector = bi->bi_sector & ~((sector_t)STRIPE_SECTORS-1);
	last_sector = bio_end_sector(bi);
	bi->bi_next = NULL;
	bi->bi_phys_segments = 1;	/* over-loaded to count active stripes */

	for (;logical_sector < last_sector; logical_sector += STRIPE_SECTORS) {
		DEFINE_WAIT(w);
		int previous;
		int seq;

	retry:
		seq = read_seqcount_begin(&conf->gen_lock);
		previous = 0;
		prepare_to_wait(&conf->wait_for_overlap, &w, TASK_UNINTERRUPTIBLE);
		if (unlikely(conf->reshape_progress != MaxSector)) {
			/* spinlock is needed as reshape_progress may be
			 * 64bit on a 32bit platform, and so it might be
			 * possible to see a half-updated value
			 * Of course reshape_progress could change after
			 * the lock is dropped, so once we get a reference
			 * to the stripe that we think it is, we will have
			 * to check again.
			 */
			spin_lock_irq(&conf->device_lock);
			if (mddev->reshape_backwards
			    ? logical_sector < conf->reshape_progress
			    : logical_sector >= conf->reshape_progress) {
				previous = 1;
			} else {
				if (mddev->reshape_backwards
				    ? logical_sector < conf->reshape_safe
				    : logical_sector >= conf->reshape_safe) {
					spin_unlock_irq(&conf->device_lock);
					schedule();
					goto retry;
				}
			}
			spin_unlock_irq(&conf->device_lock);
		}

		new_sector = raid5_compute_sector(conf, logical_sector,
						  previous,
						  &dd_idx, NULL);
		pr_debug("raid456: make_request, sector %llu logical %llu\n",
			(unsigned long long)new_sector,
			(unsigned long long)logical_sector);

		sh = get_active_stripe(conf, new_sector, previous,
				       (bi->bi_rw&RWA_MASK), 0);
		if (sh) {
			if (unlikely(previous)) {
				/* expansion might have moved on while waiting for a
				 * stripe, so we must do the range check again.
				 * Expansion could still move past after this
				 * test, but as we are holding a reference to
				 * 'sh', we know that if that happens,
				 *  STRIPE_EXPANDING will get set and the expansion
				 * won't proceed until we finish with the stripe.
				 */
				int must_retry = 0;
				spin_lock_irq(&conf->device_lock);
				if (mddev->reshape_backwards
				    ? logical_sector >= conf->reshape_progress
				    : logical_sector < conf->reshape_progress)
					/* mismatch, need to try again */
					must_retry = 1;
				spin_unlock_irq(&conf->device_lock);
				if (must_retry) {
					release_stripe(sh);
					schedule();
					goto retry;
				}
			}
			if (read_seqcount_retry(&conf->gen_lock, seq)) {
				/* Might have got the wrong stripe_head
				 * by accident
				 */
				release_stripe(sh);
				goto retry;
			}

			if (rw == WRITE &&
			    logical_sector >= mddev->suspend_lo &&
			    logical_sector < mddev->suspend_hi) {
				release_stripe(sh);
				/* As the suspend_* range is controlled by
				 * userspace, we want an interruptible
				 * wait.
				 */
				flush_signals(current);
				prepare_to_wait(&conf->wait_for_overlap,
						&w, TASK_INTERRUPTIBLE);
				if (logical_sector >= mddev->suspend_lo &&
				    logical_sector < mddev->suspend_hi)
					schedule();
				goto retry;
			}

			if (test_bit(STRIPE_EXPANDING, &sh->state) ||
			    !add_stripe_bio(sh, bi, dd_idx, rw)) {
				/* Stripe is busy expanding or
				 * add failed due to overlap.  Flush everything
				 * and wait a while
				 */
				md_wakeup_thread(mddev->thread);
				release_stripe(sh);
				schedule();
				goto retry;
			}
			finish_wait(&conf->wait_for_overlap, &w);
			set_bit(STRIPE_HANDLE, &sh->state);
			clear_bit(STRIPE_DELAYED, &sh->state);
			if ((bi->bi_rw & REQ_SYNC) &&
			    !test_and_set_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
				atomic_inc(&conf->preread_active_stripes);
			release_stripe_plug(mddev, sh);
		} else {
			/* cannot get stripe for read-ahead, just give-up */
			clear_bit(BIO_UPTODATE, &bi->bi_flags);
			finish_wait(&conf->wait_for_overlap, &w);
			break;
		}
	}

	remaining = raid5_dec_bi_active_stripes(bi);
	if (remaining == 0) {

		if ( rw == WRITE )
			md_write_end(mddev);

		trace_block_bio_complete(bdev_get_queue(bi->bi_bdev),
					 bi, 0);
		bio_endio(bi, 0);
	}
}

static sector_t raid5_size(struct mddev *mddev, sector_t sectors, int raid_disks);

static sector_t reshape_request(struct mddev *mddev, sector_t sector_nr, int *skipped)
{
	/* reshaping is quite different to recovery/resync so it is
	 * handled quite separately ... here.
	 *
	 * On each call to sync_request, we gather one chunk worth of
	 * destination stripes and flag them as expanding.
	 * Then we find all the source stripes and request reads.
	 * As the reads complete, handle_stripe will copy the data
	 * into the destination stripe and release that stripe.
	 */
	struct r5conf *conf = mddev->private;
	struct stripe_head *sh;
	sector_t first_sector, last_sector;
	int raid_disks = conf->previous_raid_disks;
	int data_disks = raid_disks - conf->max_degraded;
	int new_data_disks = conf->raid_disks - conf->max_degraded;
	int i;
	int dd_idx;
	sector_t writepos, readpos, safepos;
	sector_t stripe_addr;
	int reshape_sectors;
	struct list_head stripes;

	if (sector_nr == 0) {
		/* If restarting in the middle, skip the initial sectors */
		if (mddev->reshape_backwards &&
		    conf->reshape_progress < raid5_size(mddev, 0, 0)) {
			sector_nr = raid5_size(mddev, 0, 0)
				- conf->reshape_progress;
		} else if (!mddev->reshape_backwards &&
			   conf->reshape_progress > 0)
			sector_nr = conf->reshape_progress;
		sector_div(sector_nr, new_data_disks);
		if (sector_nr) {
			mddev->curr_resync_completed = sector_nr;
			sysfs_notify(&mddev->kobj, NULL, "sync_completed");
			*skipped = 1;
			return sector_nr;
		}
	}

	/* We need to process a full chunk at a time.
	 * If old and new chunk sizes differ, we need to process the
	 * largest of these
	 */
	if (mddev->new_chunk_sectors > mddev->chunk_sectors)
		reshape_sectors = mddev->new_chunk_sectors;
	else
		reshape_sectors = mddev->chunk_sectors;

	/* We update the metadata at least every 10 seconds, or when
	 * the data about to be copied would over-write the source of
	 * the data at the front of the range.  i.e. one new_stripe
	 * along from reshape_progress new_maps to after where
	 * reshape_safe old_maps to
	 */
	writepos = conf->reshape_progress;
	sector_div(writepos, new_data_disks);
	readpos = conf->reshape_progress;
	sector_div(readpos, data_disks);
	safepos = conf->reshape_safe;
	sector_div(safepos, data_disks);
	if (mddev->reshape_backwards) {
		writepos -= min_t(sector_t, reshape_sectors, writepos);
		readpos += reshape_sectors;
		safepos += reshape_sectors;
	} else {
		writepos += reshape_sectors;
		readpos -= min_t(sector_t, reshape_sectors, readpos);
		safepos -= min_t(sector_t, reshape_sectors, safepos);
	}

	/* Having calculated the 'writepos' possibly use it
	 * to set 'stripe_addr' which is where we will write to.
	 */
	if (mddev->reshape_backwards) {
		BUG_ON(conf->reshape_progress == 0);
		stripe_addr = writepos;
		BUG_ON((mddev->dev_sectors &
			~((sector_t)reshape_sectors - 1))
		       - reshape_sectors - stripe_addr
		       != sector_nr);
	} else {
		BUG_ON(writepos != sector_nr + reshape_sectors);
		stripe_addr = sector_nr;
	}

	/* 'writepos' is the most advanced device address we might write.
	 * 'readpos' is the least advanced device address we might read.
	 * 'safepos' is the least address recorded in the metadata as having
	 *     been reshaped.
	 * If there is a min_offset_diff, these are adjusted either by
	 * increasing the safepos/readpos if diff is negative, or
	 * increasing writepos if diff is positive.
	 * If 'readpos' is then behind 'writepos', there is no way that we can
	 * ensure safety in the face of a crash - that must be done by userspace
	 * making a backup of the data.  So in that case there is no particular
	 * rush to update metadata.
	 * Otherwise if 'safepos' is behind 'writepos', then we really need to
	 * update the metadata to advance 'safepos' to match 'readpos' so that
	 * we can be safe in the event of a crash.
	 * So we insist on updating metadata if safepos is behind writepos and
	 * readpos is beyond writepos.
	 * In any case, update the metadata every 10 seconds.
	 * Maybe that number should be configurable, but I'm not sure it is
	 * worth it.... maybe it could be a multiple of safemode_delay???
	 */
	if (conf->min_offset_diff < 0) {
		safepos += -conf->min_offset_diff;
		readpos += -conf->min_offset_diff;
	} else
		writepos += conf->min_offset_diff;

	if ((mddev->reshape_backwards
	     ? (safepos > writepos && readpos < writepos)
	     : (safepos < writepos && readpos > writepos)) ||
	    time_after(jiffies, conf->reshape_checkpoint + 10*HZ)) {
		/* Cannot proceed until we've updated the superblock... */
		wait_event(conf->wait_for_overlap,
			   atomic_read(&conf->reshape_stripes)==0);
		mddev->reshape_position = conf->reshape_progress;
		mddev->curr_resync_completed = sector_nr;
		conf->reshape_checkpoint = jiffies;
		set_bit(MD_CHANGE_DEVS, &mddev->flags);
		md_wakeup_thread(mddev->thread);
		wait_event(mddev->sb_wait, mddev->flags == 0 ||
			   kthread_should_stop());
		spin_lock_irq(&conf->device_lock);
		conf->reshape_safe = mddev->reshape_position;
		spin_unlock_irq(&conf->device_lock);
		wake_up(&conf->wait_for_overlap);
		sysfs_notify(&mddev->kobj, NULL, "sync_completed");
	}

	INIT_LIST_HEAD(&stripes);
	for (i = 0; i < reshape_sectors; i += STRIPE_SECTORS) {
		int j;
		int skipped_disk = 0;
		sh = get_active_stripe(conf, stripe_addr+i, 0, 0, 1);
		set_bit(STRIPE_EXPANDING, &sh->state);
		atomic_inc(&conf->reshape_stripes);
		/* If any of this stripe is beyond the end of the old
		 * array, then we need to zero those blocks
		 */
		for (j=sh->disks; j--;) {
			sector_t s;
			if (j == sh->pd_idx)
				continue;
			if (conf->level == 6 &&
			    j == sh->qd_idx)
				continue;
			s = compute_blocknr(sh, j, 0);
			if (s < raid5_size(mddev, 0, 0)) {
				skipped_disk = 1;
				continue;
			}
			memset(page_address(sh->dev[j].page), 0, STRIPE_SIZE);
			set_bit(R5_Expanded, &sh->dev[j].flags);
			set_bit(R5_UPTODATE, &sh->dev[j].flags);
		}
		if (!skipped_disk) {
			set_bit(STRIPE_EXPAND_READY, &sh->state);
			set_bit(STRIPE_HANDLE, &sh->state);
		}
		list_add(&sh->lru, &stripes);
	}
	spin_lock_irq(&conf->device_lock);
	if (mddev->reshape_backwards)
		conf->reshape_progress -= reshape_sectors * new_data_disks;
	else
		conf->reshape_progress += reshape_sectors * new_data_disks;
	spin_unlock_irq(&conf->device_lock);
	/* Ok, those stripe are ready. We can start scheduling
	 * reads on the source stripes.
	 * The source stripes are determined by mapping the first and last
	 * block on the destination stripes.
	 */
	first_sector =
		raid5_compute_sector(conf, stripe_addr*(new_data_disks),
				     1, &dd_idx, NULL);
	last_sector =
		raid5_compute_sector(conf, ((stripe_addr+reshape_sectors)
					    * new_data_disks - 1),
				     1, &dd_idx, NULL);
	if (last_sector >= mddev->dev_sectors)
		last_sector = mddev->dev_sectors - 1;
	while (first_sector <= last_sector) {
		sh = get_active_stripe(conf, first_sector, 1, 0, 1);
		set_bit(STRIPE_EXPAND_SOURCE, &sh->state);
		set_bit(STRIPE_HANDLE, &sh->state);
		release_stripe(sh);
		first_sector += STRIPE_SECTORS;
	}
	/* Now that the sources are clearly marked, we can release
	 * the destination stripes
	 */
	while (!list_empty(&stripes)) {
		sh = list_entry(stripes.next, struct stripe_head, lru);
		list_del_init(&sh->lru);
		release_stripe(sh);
	}
	/* If this takes us to the resync_max point where we have to pause,
	 * then we need to write out the superblock.
	 */
	sector_nr += reshape_sectors;
	if ((sector_nr - mddev->curr_resync_completed) * 2
	    >= mddev->resync_max - mddev->curr_resync_completed) {
		/* Cannot proceed until we've updated the superblock... */
		wait_event(conf->wait_for_overlap,
			   atomic_read(&conf->reshape_stripes) == 0);
		mddev->reshape_position = conf->reshape_progress;
		mddev->curr_resync_completed = sector_nr;
		conf->reshape_checkpoint = jiffies;
		set_bit(MD_CHANGE_DEVS, &mddev->flags);
		md_wakeup_thread(mddev->thread);
		wait_event(mddev->sb_wait,
			   !test_bit(MD_CHANGE_DEVS, &mddev->flags)
			   || kthread_should_stop());
		spin_lock_irq(&conf->device_lock);
		conf->reshape_safe = mddev->reshape_position;
		spin_unlock_irq(&conf->device_lock);
		wake_up(&conf->wait_for_overlap);
		sysfs_notify(&mddev->kobj, NULL, "sync_completed");
	}
	return reshape_sectors;
}

/* FIXME go_faster isn't used */
static inline sector_t sync_request(struct mddev *mddev, sector_t sector_nr, int *skipped, int go_faster)
{
	struct r5conf *conf = mddev->private;
	struct stripe_head *sh;
	sector_t max_sector = mddev->dev_sectors;
	sector_t sync_blocks;
	int still_degraded = 0;
	int i;

	if (sector_nr >= max_sector) {
		/* just being told to finish up .. nothing much to do */

		if (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery)) {
			end_reshape(conf);
			return 0;
		}

		if (mddev->curr_resync < max_sector) /* aborted */
			bitmap_end_sync(mddev->bitmap, mddev->curr_resync,
					&sync_blocks, 1);
		else /* completed sync */
			conf->fullsync = 0;
		bitmap_close_sync(mddev->bitmap);

		return 0;
	}

	/* Allow raid5_quiesce to complete */
	wait_event(conf->wait_for_overlap, conf->quiesce != 2);

	if (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery))
		return reshape_request(mddev, sector_nr, skipped);

	/* No need to check resync_max as we never do more than one
	 * stripe, and as resync_max will always be on a chunk boundary,
	 * if the check in md_do_sync didn't fire, there is no chance
	 * of overstepping resync_max here
	 */

	/* if there is too many failed drives and we are trying
	 * to resync, then assert that we are finished, because there is
	 * nothing we can do.
	 */
	if (mddev->degraded >= conf->max_degraded &&
	    test_bit(MD_RECOVERY_SYNC, &mddev->recovery)) {
		sector_t rv = mddev->dev_sectors - sector_nr;
		*skipped = 1;
		return rv;
	}
	if (!test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery) &&
	    !conf->fullsync &&
	    !bitmap_start_sync(mddev->bitmap, sector_nr, &sync_blocks, 1) &&
	    sync_blocks >= STRIPE_SECTORS) {
		/* we can skip this block, and probably more */
		sync_blocks /= STRIPE_SECTORS;
		*skipped = 1;
		return sync_blocks * STRIPE_SECTORS; /* keep things rounded to whole stripes */
	}

	bitmap_cond_end_sync(mddev->bitmap, sector_nr);

	sh = get_active_stripe(conf, sector_nr, 0, 1, 0);
	if (sh == NULL) {
		sh = get_active_stripe(conf, sector_nr, 0, 0, 0);
		/* make sure we don't swamp the stripe cache if someone else
		 * is trying to get access
		 */
		schedule_timeout_uninterruptible(1);
	}
	/* Need to check if array will still be degraded after recovery/resync
	 * We don't need to check the 'failed' flag as when that gets set,
	 * recovery aborts.
	 */
	for (i = 0; i < conf->raid_disks; i++)
		if (conf->disks[i].rdev == NULL)
			still_degraded = 1;

	bitmap_start_sync(mddev->bitmap, sector_nr, &sync_blocks, still_degraded);

	set_bit(STRIPE_SYNC_REQUESTED, &sh->state);

	handle_stripe(sh);
	release_stripe(sh);

	return STRIPE_SECTORS;
}

static int  retry_aligned_read(struct r5conf *conf, struct bio *raid_bio)
{
	/* We may not be able to submit a whole bio at once as there
	 * may not be enough stripe_heads available.
	 * We cannot pre-allocate enough stripe_heads as we may need
	 * more than exist in the cache (if we allow ever large chunks).
	 * So we do one stripe head at a time and record in
	 * ->bi_hw_segments how many have been done.
	 *
	 * We *know* that this entire raid_bio is in one chunk, so
	 * it will be only one 'dd_idx' and only need one call to raid5_compute_sector.
	 */
	struct stripe_head *sh;
	int dd_idx;
	sector_t sector, logical_sector, last_sector;
	int scnt = 0;
	int remaining;
	int handled = 0;

	logical_sector = raid_bio->bi_sector & ~((sector_t)STRIPE_SECTORS-1);
	sector = raid5_compute_sector(conf, logical_sector,
				      0, &dd_idx, NULL);
	last_sector = bio_end_sector(raid_bio);

	for (; logical_sector < last_sector;
	     logical_sector += STRIPE_SECTORS,
		     sector += STRIPE_SECTORS,
		     scnt++) {

		if (scnt < raid5_bi_processed_stripes(raid_bio))
			/* already done this stripe */
			continue;

		sh = get_active_stripe(conf, sector, 0, 1, 0);

		if (!sh) {
			/* failed to get a stripe - must wait */
			raid5_set_bi_processed_stripes(raid_bio, scnt);
			conf->retry_read_aligned = raid_bio;
			return handled;
		}

		if (!add_stripe_bio(sh, raid_bio, dd_idx, 0)) {
			release_stripe(sh);
			raid5_set_bi_processed_stripes(raid_bio, scnt);
			conf->retry_read_aligned = raid_bio;
			return handled;
		}

		set_bit(R5_ReadNoMerge, &sh->dev[dd_idx].flags);
		handle_stripe(sh);
		release_stripe(sh);
		handled++;
	}
	remaining = raid5_dec_bi_active_stripes(raid_bio);
	if (remaining == 0) {
		trace_block_bio_complete(bdev_get_queue(raid_bio->bi_bdev),
					 raid_bio, 0);
		bio_endio(raid_bio, 0);
	}
	if (atomic_dec_and_test(&conf->active_aligned_reads))
		wake_up(&conf->wait_for_stripe);
	return handled;
}

#define MAX_STRIPE_BATCH 8
static int handle_active_stripes(struct r5conf *conf, int group)
{
	struct stripe_head *batch[MAX_STRIPE_BATCH], *sh;
	int i, batch_size = 0;

	while (batch_size < MAX_STRIPE_BATCH &&
			(sh = __get_priority_stripe(conf, group)) != NULL)
		batch[batch_size++] = sh;

	if (batch_size == 0)
		return batch_size;
	spin_unlock_irq(&conf->device_lock);

	for (i = 0; i < batch_size; i++)
		handle_stripe(batch[i]);

	cond_resched();

	spin_lock_irq(&conf->device_lock);
	for (i = 0; i < batch_size; i++)
		__release_stripe(conf, batch[i]);
	return batch_size;
}

static void raid5_do_work(struct work_struct *work)
{
	struct r5worker *worker = container_of(work, struct r5worker, work);
	struct r5worker_group *group = worker->group;
	struct r5conf *conf = group->conf;
	int group_id = group - conf->worker_groups;
	int handled;
	struct blk_plug plug;

	pr_debug("+++ raid5worker active\n");

	blk_start_plug(&plug);
	handled = 0;
	spin_lock_irq(&conf->device_lock);
	while (1) {
		int batch_size, released;

		released = release_stripe_list(conf);

		batch_size = handle_active_stripes(conf, group_id);
		if (!batch_size && !released)
			break;
		handled += batch_size;
	}
	pr_debug("%d stripes handled\n", handled);

	spin_unlock_irq(&conf->device_lock);
	blk_finish_plug(&plug);

	pr_debug("--- raid5worker inactive\n");
}

/*
 * This is our raid5 kernel thread.
 *
 * We scan the hash table for stripes which can be handled now.
 * During the scan, completed stripes are saved for us by the interrupt
 * handler, so that they will not have to wait for our next wakeup.
 */
static void raid5d(struct md_thread *thread)
{
	struct mddev *mddev = thread->mddev;
	struct r5conf *conf = mddev->private;
	int handled;
	struct blk_plug plug;

	pr_debug("+++ raid5d active\n");

	md_check_recovery(mddev);

	blk_start_plug(&plug);
	handled = 0;
	spin_lock_irq(&conf->device_lock);
	while (1) {
		struct bio *bio;
		int batch_size, released;

		released = release_stripe_list(conf);

		if (
		    !list_empty(&conf->bitmap_list)) {
			/* Now is a good time to flush some bitmap updates */
			conf->seq_flush++;
			spin_unlock_irq(&conf->device_lock);
			bitmap_unplug(mddev->bitmap);
			spin_lock_irq(&conf->device_lock);
			conf->seq_write = conf->seq_flush;
			activate_bit_delay(conf);
		}
		raid5_activate_delayed(conf);

		while ((bio = remove_bio_from_retry(conf))) {
			int ok;
			spin_unlock_irq(&conf->device_lock);
			ok = retry_aligned_read(conf, bio);
			spin_lock_irq(&conf->device_lock);
			if (!ok)
				break;
			handled++;
		}

		batch_size = handle_active_stripes(conf, ANY_GROUP);
		if (!batch_size && !released)
			break;
		handled += batch_size;

		if (mddev->flags & ~(1<<MD_CHANGE_PENDING)) {
			spin_unlock_irq(&conf->device_lock);
			md_check_recovery(mddev);
			spin_lock_irq(&conf->device_lock);
		}
	}
	pr_debug("%d stripes handled\n", handled);

	spin_unlock_irq(&conf->device_lock);

	async_tx_issue_pending_all();
	blk_finish_plug(&plug);

	pr_debug("--- raid5d inactive\n");
}

static ssize_t
raid5_show_stripe_cache_size(struct mddev *mddev, char *page)
{
	struct r5conf *conf = mddev->private;
	if (conf)
		return sprintf(page, "%d\n", conf->max_nr_stripes);
	else
		return 0;
}

int
raid5_set_cache_size(struct mddev *mddev, int size)
{
	struct r5conf *conf = mddev->private;
	int err;

	if (size <= 16 || size > 32768)
		return -EINVAL;
	while (size < conf->max_nr_stripes) {
		if (drop_one_stripe(conf))
			conf->max_nr_stripes--;
		else
			break;
	}
	err = md_allow_write(mddev);
	if (err)
		return err;
	while (size > conf->max_nr_stripes) {
		if (grow_one_stripe(conf))
			conf->max_nr_stripes++;
		else break;
	}
	return 0;
}
EXPORT_SYMBOL(raid5_set_cache_size);

static ssize_t
raid5_store_stripe_cache_size(struct mddev *mddev, const char *page, size_t len)
{
	struct r5conf *conf = mddev->private;
	unsigned long new;
	int err;

	if (len >= PAGE_SIZE)
		return -EINVAL;
	if (!conf)
		return -ENODEV;

	if (kstrtoul(page, 10, &new))
		return -EINVAL;
	err = raid5_set_cache_size(mddev, new);
	if (err)
		return err;
	return len;
}

static struct md_sysfs_entry
raid5_stripecache_size = __ATTR(stripe_cache_size, S_IRUGO | S_IWUSR,
				raid5_show_stripe_cache_size,
				raid5_store_stripe_cache_size);

static ssize_t
raid5_show_preread_threshold(struct mddev *mddev, char *page)
{
	struct r5conf *conf = mddev->private;
	if (conf)
		return sprintf(page, "%d\n", conf->bypass_threshold);
	else
		return 0;
}

static ssize_t
raid5_store_preread_threshold(struct mddev *mddev, const char *page, size_t len)
{
	struct r5conf *conf = mddev->private;
	unsigned long new;
	if (len >= PAGE_SIZE)
		return -EINVAL;
	if (!conf)
		return -ENODEV;

	if (kstrtoul(page, 10, &new))
		return -EINVAL;
	if (new > conf->max_nr_stripes)
		return -EINVAL;
	conf->bypass_threshold = new;
	return len;
}

static struct md_sysfs_entry
raid5_preread_bypass_threshold = __ATTR(preread_bypass_threshold,
					S_IRUGO | S_IWUSR,
					raid5_show_preread_threshold,
					raid5_store_preread_threshold);

static ssize_t
stripe_cache_active_show(struct mddev *mddev, char *page)
{
	struct r5conf *conf = mddev->private;
	if (conf)
		return sprintf(page, "%d\n", atomic_read(&conf->active_stripes));
	else
		return 0;
}

static struct md_sysfs_entry
raid5_stripecache_active = __ATTR_RO(stripe_cache_active);

static ssize_t
raid5_show_group_thread_cnt(struct mddev *mddev, char *page)
{
	struct r5conf *conf = mddev->private;
	if (conf)
		return sprintf(page, "%d\n", conf->worker_cnt_per_group);
	else
		return 0;
}

static int alloc_thread_groups(struct r5conf *conf, int cnt);
static ssize_t
raid5_store_group_thread_cnt(struct mddev *mddev, const char *page, size_t len)
{
	struct r5conf *conf = mddev->private;
	unsigned long new;
	int err;
	struct r5worker_group *old_groups;
	int old_group_cnt;

	if (len >= PAGE_SIZE)
		return -EINVAL;
	if (!conf)
		return -ENODEV;

	if (kstrtoul(page, 10, &new))
		return -EINVAL;

	if (new == conf->worker_cnt_per_group)
		return len;

	mddev_suspend(mddev);

	old_groups = conf->worker_groups;
	old_group_cnt = conf->worker_cnt_per_group;

	conf->worker_groups = NULL;
	err = alloc_thread_groups(conf, new);
	if (err) {
		conf->worker_groups = old_groups;
		conf->worker_cnt_per_group = old_group_cnt;
	} else {
		if (old_groups)
			kfree(old_groups[0].workers);
		kfree(old_groups);
	}

	mddev_resume(mddev);

	if (err)
		return err;
	return len;
}

static struct md_sysfs_entry
raid5_group_thread_cnt = __ATTR(group_thread_cnt, S_IRUGO | S_IWUSR,
				raid5_show_group_thread_cnt,
				raid5_store_group_thread_cnt);

static struct attribute *raid5_attrs[] =  {
	&raid5_stripecache_size.attr,
	&raid5_stripecache_active.attr,
	&raid5_preread_bypass_threshold.attr,
	&raid5_group_thread_cnt.attr,
	NULL,
};
static struct attribute_group raid5_attrs_group = {
	.name = NULL,
	.attrs = raid5_attrs,
};

static int alloc_thread_groups(struct r5conf *conf, int cnt)
{
	int i, j;
	ssize_t size;
	struct r5worker *workers;

	conf->worker_cnt_per_group = cnt;
	if (cnt == 0) {
		conf->worker_groups = NULL;
		return 0;
	}
	conf->group_cnt = num_possible_nodes();
	size = sizeof(struct r5worker) * cnt;
	workers = kzalloc(size * conf->group_cnt, GFP_NOIO);
	conf->worker_groups = kzalloc(sizeof(struct r5worker_group) *
				conf->group_cnt, GFP_NOIO);
	if (!conf->worker_groups || !workers) {
		kfree(workers);
		kfree(conf->worker_groups);
		conf->worker_groups = NULL;
		return -ENOMEM;
	}

	for (i = 0; i < conf->group_cnt; i++) {
		struct r5worker_group *group;

		group = &conf->worker_groups[i];
		INIT_LIST_HEAD(&group->handle_list);
		group->conf = conf;
		group->workers = workers + i * cnt;

		for (j = 0; j < cnt; j++) {
			group->workers[j].group = group;
			INIT_WORK(&group->workers[j].work, raid5_do_work);
		}
	}

	return 0;
}

static void free_thread_groups(struct r5conf *conf)
{
	if (conf->worker_groups)
		kfree(conf->worker_groups[0].workers);
	kfree(conf->worker_groups);
	conf->worker_groups = NULL;
}

static sector_t
raid5_size(struct mddev *mddev, sector_t sectors, int raid_disks)
{
	struct r5conf *conf = mddev->private;

	if (!sectors)
		sectors = mddev->dev_sectors;
	if (!raid_disks)
		/* size is defined by the smallest of previous and new size */
		raid_disks = min(conf->raid_disks, conf->previous_raid_disks);

	sectors &= ~((sector_t)mddev->chunk_sectors - 1);
	sectors &= ~((sector_t)mddev->new_chunk_sectors - 1);
	return sectors * (raid_disks - conf->max_degraded);
}

static void raid5_free_percpu(struct r5conf *conf)
{
	struct raid5_percpu *percpu;
	unsigned long cpu;

	if (!conf->percpu)
		return;

	get_online_cpus();
	for_each_possible_cpu(cpu) {
		percpu = per_cpu_ptr(conf->percpu, cpu);
		safe_put_page(percpu->spare_page);
		kfree(percpu->scribble);
	}
#ifdef CONFIG_HOTPLUG_CPU
	unregister_cpu_notifier(&conf->cpu_notify);
#endif
	put_online_cpus();

	free_percpu(conf->percpu);
}

static void free_conf(struct r5conf *conf)
{
	free_thread_groups(conf);
	shrink_stripes(conf);
	raid5_free_percpu(conf);
	kfree(conf->disks);
	kfree(conf->stripe_hashtbl);
	kfree(conf);
}

#ifdef CONFIG_HOTPLUG_CPU
static int raid456_cpu_notify(struct notifier_block *nfb, unsigned long action,
			      void *hcpu)
{
	struct r5conf *conf = container_of(nfb, struct r5conf, cpu_notify);
	long cpu = (long)hcpu;
	struct raid5_percpu *percpu = per_cpu_ptr(conf->percpu, cpu);

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		if (conf->level == 6 && !percpu->spare_page)
			percpu->spare_page = alloc_page(GFP_KERNEL);
		if (!percpu->scribble)
			percpu->scribble = kmalloc(conf->scribble_len, GFP_KERNEL);

		if (!percpu->scribble ||
		    (conf->level == 6 && !percpu->spare_page)) {
			safe_put_page(percpu->spare_page);
			kfree(percpu->scribble);
			pr_err("%s: failed memory allocation for cpu%ld\n",
			       __func__, cpu);
			return notifier_from_errno(-ENOMEM);
		}
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		safe_put_page(percpu->spare_page);
		kfree(percpu->scribble);
		percpu->spare_page = NULL;
		percpu->scribble = NULL;
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}
#endif

static int raid5_alloc_percpu(struct r5conf *conf)
{
	unsigned long cpu;
	struct page *spare_page;
	struct raid5_percpu __percpu *allcpus;
	void *scribble;
	int err;

	allcpus = alloc_percpu(struct raid5_percpu);
	if (!allcpus)
		return -ENOMEM;
	conf->percpu = allcpus;

	get_online_cpus();
	err = 0;
	for_each_present_cpu(cpu) {
		if (conf->level == 6) {
			spare_page = alloc_page(GFP_KERNEL);
			if (!spare_page) {
				err = -ENOMEM;
				break;
			}
			per_cpu_ptr(conf->percpu, cpu)->spare_page = spare_page;
		}
		scribble = kmalloc(conf->scribble_len, GFP_KERNEL);
		if (!scribble) {
			err = -ENOMEM;
			break;
		}
		per_cpu_ptr(conf->percpu, cpu)->scribble = scribble;
	}
#ifdef CONFIG_HOTPLUG_CPU
	conf->cpu_notify.notifier_call = raid456_cpu_notify;
	conf->cpu_notify.priority = 0;
	if (err == 0)
		err = register_cpu_notifier(&conf->cpu_notify);
#endif
	put_online_cpus();

	return err;
}

static struct r5conf *setup_conf(struct mddev *mddev)
{
	struct r5conf *conf;
	int raid_disk, memory, max_disks;
	struct md_rdev *rdev;
	struct disk_info *disk;
	char pers_name[6];

	if (mddev->new_level != 5
	    && mddev->new_level != 4
	    && mddev->new_level != 6) {
		printk(KERN_ERR "md/raid:%s: raid level not set to 4/5/6 (%d)\n",
		       mdname(mddev), mddev->new_level);
		return ERR_PTR(-EIO);
	}
	if ((mddev->new_level == 5
	     && !algorithm_valid_raid5(mddev->new_layout)) ||
	    (mddev->new_level == 6
	     && !algorithm_valid_raid6(mddev->new_layout))) {
		printk(KERN_ERR "md/raid:%s: layout %d not supported\n",
		       mdname(mddev), mddev->new_layout);
		return ERR_PTR(-EIO);
	}
	if (mddev->new_level == 6 && mddev->raid_disks < 4) {
		printk(KERN_ERR "md/raid:%s: not enough configured devices (%d, minimum 4)\n",
		       mdname(mddev), mddev->raid_disks);
		return ERR_PTR(-EINVAL);
	}

	if (!mddev->new_chunk_sectors ||
	    (mddev->new_chunk_sectors << 9) % PAGE_SIZE ||
	    !is_power_of_2(mddev->new_chunk_sectors)) {
		printk(KERN_ERR "md/raid:%s: invalid chunk size %d\n",
		       mdname(mddev), mddev->new_chunk_sectors << 9);
		return ERR_PTR(-EINVAL);
	}

	conf = kzalloc(sizeof(struct r5conf), GFP_KERNEL);
	if (conf == NULL)
		goto abort;
	/* Don't enable multi-threading by default*/
	if (alloc_thread_groups(conf, 0))
		goto abort;
	spin_lock_init(&conf->device_lock);
	seqcount_init(&conf->gen_lock);
	init_waitqueue_head(&conf->wait_for_stripe);
	init_waitqueue_head(&conf->wait_for_overlap);
	INIT_LIST_HEAD(&conf->handle_list);
	INIT_LIST_HEAD(&conf->hold_list);
	INIT_LIST_HEAD(&conf->delayed_list);
	INIT_LIST_HEAD(&conf->bitmap_list);
	INIT_LIST_HEAD(&conf->inactive_list);
	init_llist_head(&conf->released_stripes);
	atomic_set(&conf->active_stripes, 0);
	atomic_set(&conf->preread_active_stripes, 0);
	atomic_set(&conf->active_aligned_reads, 0);
	conf->bypass_threshold = BYPASS_THRESHOLD;
	conf->recovery_disabled = mddev->recovery_disabled - 1;

	conf->raid_disks = mddev->raid_disks;
	if (mddev->reshape_position == MaxSector)
		conf->previous_raid_disks = mddev->raid_disks;
	else
		conf->previous_raid_disks = mddev->raid_disks - mddev->delta_disks;
	max_disks = max(conf->raid_disks, conf->previous_raid_disks);
	conf->scribble_len = scribble_len(max_disks);

	conf->disks = kzalloc(max_disks * sizeof(struct disk_info),
			      GFP_KERNEL);
	if (!conf->disks)
		goto abort;

	conf->mddev = mddev;

	if ((conf->stripe_hashtbl = kzalloc(PAGE_SIZE, GFP_KERNEL)) == NULL)
		goto abort;

	conf->level = mddev->new_level;
	if (raid5_alloc_percpu(conf) != 0)
		goto abort;

	pr_debug("raid456: run(%s) called.\n", mdname(mddev));

	rdev_for_each(rdev, mddev) {
		raid_disk = rdev->raid_disk;
		if (raid_disk >= max_disks
		    || raid_disk < 0)
			continue;
		disk = conf->disks + raid_disk;

		if (test_bit(Replacement, &rdev->flags)) {
			if (disk->replacement)
				goto abort;
			disk->replacement = rdev;
		} else {
			if (disk->rdev)
				goto abort;
			disk->rdev = rdev;
		}

		if (test_bit(In_sync, &rdev->flags)) {
			char b[BDEVNAME_SIZE];
			printk(KERN_INFO "md/raid:%s: device %s operational as raid"
			       " disk %d\n",
			       mdname(mddev), bdevname(rdev->bdev, b), raid_disk);
		} else if (rdev->saved_raid_disk != raid_disk)
			/* Cannot rely on bitmap to complete recovery */
			conf->fullsync = 1;
	}

	conf->chunk_sectors = mddev->new_chunk_sectors;
	conf->level = mddev->new_level;
	if (conf->level == 6)
		conf->max_degraded = 2;
	else
		conf->max_degraded = 1;
	conf->algorithm = mddev->new_layout;
	conf->max_nr_stripes = NR_STRIPES;
	conf->reshape_progress = mddev->reshape_position;
	if (conf->reshape_progress != MaxSector) {
		conf->prev_chunk_sectors = mddev->chunk_sectors;
		conf->prev_algo = mddev->layout;
	}

	memory = conf->max_nr_stripes * (sizeof(struct stripe_head) +
		 max_disks * ((sizeof(struct bio) + PAGE_SIZE))) / 1024;
	if (grow_stripes(conf, conf->max_nr_stripes)) {
		printk(KERN_ERR
		       "md/raid:%s: couldn't allocate %dkB for buffers\n",
		       mdname(mddev), memory);
		goto abort;
	} else
		printk(KERN_INFO "md/raid:%s: allocated %dkB\n",
		       mdname(mddev), memory);

	sprintf(pers_name, "raid%d", mddev->new_level);
	conf->thread = md_register_thread(raid5d, mddev, pers_name);
	if (!conf->thread) {
		printk(KERN_ERR
		       "md/raid:%s: couldn't allocate thread.\n",
		       mdname(mddev));
		goto abort;
	}

	return conf;

 abort:
	if (conf) {
		free_conf(conf);
		return ERR_PTR(-EIO);
	} else
		return ERR_PTR(-ENOMEM);
}


static int only_parity(int raid_disk, int algo, int raid_disks, int max_degraded)
{
	switch (algo) {
	case ALGORITHM_PARITY_0:
		if (raid_disk < max_degraded)
			return 1;
		break;
	case ALGORITHM_PARITY_N:
		if (raid_disk >= raid_disks - max_degraded)
			return 1;
		break;
	case ALGORITHM_PARITY_0_6:
		if (raid_disk == 0 || 
		    raid_disk == raid_disks - 1)
			return 1;
		break;
	case ALGORITHM_LEFT_ASYMMETRIC_6:
	case ALGORITHM_RIGHT_ASYMMETRIC_6:
	case ALGORITHM_LEFT_SYMMETRIC_6:
	case ALGORITHM_RIGHT_SYMMETRIC_6:
		if (raid_disk == raid_disks - 1)
			return 1;
	}
	return 0;
}

static int run(struct mddev *mddev)
{
	struct r5conf *conf;
	int working_disks = 0;
	int dirty_parity_disks = 0;
	struct md_rdev *rdev;
	sector_t reshape_offset = 0;
	int i;
	long long min_offset_diff = 0;
	int first = 1;

	if (mddev->recovery_cp != MaxSector)
		printk(KERN_NOTICE "md/raid:%s: not clean"
		       " -- starting background reconstruction\n",
		       mdname(mddev));

	rdev_for_each(rdev, mddev) {
		long long diff;
		if (rdev->raid_disk < 0)
			continue;
		diff = (rdev->new_data_offset - rdev->data_offset);
		if (first) {
			min_offset_diff = diff;
			first = 0;
		} else if (mddev->reshape_backwards &&
			 diff < min_offset_diff)
			min_offset_diff = diff;
		else if (!mddev->reshape_backwards &&
			 diff > min_offset_diff)
			min_offset_diff = diff;
	}

	if (mddev->reshape_position != MaxSector) {
		/* Check that we can continue the reshape.
		 * Difficulties arise if the stripe we would write to
		 * next is at or after the stripe we would read from next.
		 * For a reshape that changes the number of devices, this
		 * is only possible for a very short time, and mdadm makes
		 * sure that time appears to have past before assembling
		 * the array.  So we fail if that time hasn't passed.
		 * For a reshape that keeps the number of devices the same
		 * mdadm must be monitoring the reshape can keeping the
		 * critical areas read-only and backed up.  It will start
		 * the array in read-only mode, so we check for that.
		 */
		sector_t here_new, here_old;
		int old_disks;
		int max_degraded = (mddev->level == 6 ? 2 : 1);

		if (mddev->new_level != mddev->level) {
			printk(KERN_ERR "md/raid:%s: unsupported reshape "
			       "required - aborting.\n",
			       mdname(mddev));
			return -EINVAL;
		}
		old_disks = mddev->raid_disks - mddev->delta_disks;
		/* reshape_position must be on a new-stripe boundary, and one
		 * further up in new geometry must map after here in old
		 * geometry.
		 */
		here_new = mddev->reshape_position;
		if (sector_div(here_new, mddev->new_chunk_sectors *
			       (mddev->raid_disks - max_degraded))) {
			printk(KERN_ERR "md/raid:%s: reshape_position not "
			       "on a stripe boundary\n", mdname(mddev));
			return -EINVAL;
		}
		reshape_offset = here_new * mddev->new_chunk_sectors;
		/* here_new is the stripe we will write to */
		here_old = mddev->reshape_position;
		sector_div(here_old, mddev->chunk_sectors *
			   (old_disks-max_degraded));
		/* here_old is the first stripe that we might need to read
		 * from */
		if (mddev->delta_disks == 0) {
			if ((here_new * mddev->new_chunk_sectors !=
			     here_old * mddev->chunk_sectors)) {
				printk(KERN_ERR "md/raid:%s: reshape position is"
				       " confused - aborting\n", mdname(mddev));
				return -EINVAL;
			}
			/* We cannot be sure it is safe to start an in-place
			 * reshape.  It is only safe if user-space is monitoring
			 * and taking constant backups.
			 * mdadm always starts a situation like this in
			 * readonly mode so it can take control before
			 * allowing any writes.  So just check for that.
			 */
			if (abs(min_offset_diff) >= mddev->chunk_sectors &&
			    abs(min_offset_diff) >= mddev->new_chunk_sectors)
				/* not really in-place - so OK */;
			else if (mddev->ro == 0) {
				printk(KERN_ERR "md/raid:%s: in-place reshape "
				       "must be started in read-only mode "
				       "- aborting\n",
				       mdname(mddev));
				return -EINVAL;
			}
		} else if (mddev->reshape_backwards
		    ? (here_new * mddev->new_chunk_sectors + min_offset_diff <=
		       here_old * mddev->chunk_sectors)
		    : (here_new * mddev->new_chunk_sectors >=
		       here_old * mddev->chunk_sectors + (-min_offset_diff))) {
			/* Reading from the same stripe as writing to - bad */
			printk(KERN_ERR "md/raid:%s: reshape_position too early for "
			       "auto-recovery - aborting.\n",
			       mdname(mddev));
			return -EINVAL;
		}
		printk(KERN_INFO "md/raid:%s: reshape will continue\n",
		       mdname(mddev));
		/* OK, we should be able to continue; */
	} else {
		BUG_ON(mddev->level != mddev->new_level);
		BUG_ON(mddev->layout != mddev->new_layout);
		BUG_ON(mddev->chunk_sectors != mddev->new_chunk_sectors);
		BUG_ON(mddev->delta_disks != 0);
	}

	if (mddev->private == NULL)
		conf = setup_conf(mddev);
	else
		conf = mddev->private;

	if (IS_ERR(conf))
		return PTR_ERR(conf);

	conf->min_offset_diff = min_offset_diff;
	mddev->thread = conf->thread;
	conf->thread = NULL;
	mddev->private = conf;

	for (i = 0; i < conf->raid_disks && conf->previous_raid_disks;
	     i++) {
		rdev = conf->disks[i].rdev;
		if (!rdev && conf->disks[i].replacement) {
			/* The replacement is all we have yet */
			rdev = conf->disks[i].replacement;
			conf->disks[i].replacement = NULL;
			clear_bit(Replacement, &rdev->flags);
			conf->disks[i].rdev = rdev;
		}
		if (!rdev)
			continue;
		if (conf->disks[i].replacement &&
		    conf->reshape_progress != MaxSector) {
			/* replacements and reshape simply do not mix. */
			printk(KERN_ERR "md: cannot handle concurrent "
			       "replacement and reshape.\n");
			goto abort;
		}
		if (test_bit(In_sync, &rdev->flags)) {
			working_disks++;
			continue;
		}
		/* This disc is not fully in-sync.  However if it
		 * just stored parity (beyond the recovery_offset),
		 * when we don't need to be concerned about the
		 * array being dirty.
		 * When reshape goes 'backwards', we never have
		 * partially completed devices, so we only need
		 * to worry about reshape going forwards.
		 */
		/* Hack because v0.91 doesn't store recovery_offset properly. */
		if (mddev->major_version == 0 &&
		    mddev->minor_version > 90)
			rdev->recovery_offset = reshape_offset;

		if (rdev->recovery_offset < reshape_offset) {
			/* We need to check old and new layout */
			if (!only_parity(rdev->raid_disk,
					 conf->algorithm,
					 conf->raid_disks,
					 conf->max_degraded))
				continue;
		}
		if (!only_parity(rdev->raid_disk,
				 conf->prev_algo,
				 conf->previous_raid_disks,
				 conf->max_degraded))
			continue;
		dirty_parity_disks++;
	}

	/*
	 * 0 for a fully functional array, 1 or 2 for a degraded array.
	 */
	mddev->degraded = calc_degraded(conf);

	if (has_failed(conf)) {
		printk(KERN_ERR "md/raid:%s: not enough operational devices"
			" (%d/%d failed)\n",
			mdname(mddev), mddev->degraded, conf->raid_disks);
		goto abort;
	}

	/* device size must be a multiple of chunk size */
	mddev->dev_sectors &= ~(mddev->chunk_sectors - 1);
	mddev->resync_max_sectors = mddev->dev_sectors;

	if (mddev->degraded > dirty_parity_disks &&
	    mddev->recovery_cp != MaxSector) {
		if (mddev->ok_start_degraded)
			printk(KERN_WARNING
			       "md/raid:%s: starting dirty degraded array"
			       " - data corruption possible.\n",
			       mdname(mddev));
		else {
			printk(KERN_ERR
			       "md/raid:%s: cannot start dirty degraded array.\n",
			       mdname(mddev));
			goto abort;
		}
	}

	if (mddev->degraded == 0)
		printk(KERN_INFO "md/raid:%s: raid level %d active with %d out of %d"
		       " devices, algorithm %d\n", mdname(mddev), conf->level,
		       mddev->raid_disks-mddev->degraded, mddev->raid_disks,
		       mddev->new_layout);
	else
		printk(KERN_ALERT "md/raid:%s: raid level %d active with %d"
		       " out of %d devices, algorithm %d\n",
		       mdname(mddev), conf->level,
		       mddev->raid_disks - mddev->degraded,
		       mddev->raid_disks, mddev->new_layout);

	print_raid5_conf(conf);

	if (conf->reshape_progress != MaxSector) {
		conf->reshape_safe = conf->reshape_progress;
		atomic_set(&conf->reshape_stripes, 0);
		clear_bit(MD_RECOVERY_SYNC, &mddev->recovery);
		clear_bit(MD_RECOVERY_CHECK, &mddev->recovery);
		set_bit(MD_RECOVERY_RESHAPE, &mddev->recovery);
		set_bit(MD_RECOVERY_RUNNING, &mddev->recovery);
		mddev->sync_thread = md_register_thread(md_do_sync, mddev,
							"reshape");
	}


	/* Ok, everything is just fine now */
	if (mddev->to_remove == &raid5_attrs_group)
		mddev->to_remove = NULL;
	else if (mddev->kobj.sd &&
	    sysfs_create_group(&mddev->kobj, &raid5_attrs_group))
		printk(KERN_WARNING
		       "raid5: failed to create sysfs attributes for %s\n",
		       mdname(mddev));
	md_set_array_sectors(mddev, raid5_size(mddev, 0, 0));

	if (mddev->queue) {
		int chunk_size;
		bool discard_supported = true;
		/* read-ahead size must cover two whole stripes, which
		 * is 2 * (datadisks) * chunksize where 'n' is the
		 * number of raid devices
		 */
		int data_disks = conf->previous_raid_disks - conf->max_degraded;
		int stripe = data_disks *
			((mddev->chunk_sectors << 9) / PAGE_SIZE);
		if (mddev->queue->backing_dev_info.ra_pages < 2 * stripe)
			mddev->queue->backing_dev_info.ra_pages = 2 * stripe;

		blk_queue_merge_bvec(mddev->queue, raid5_mergeable_bvec);

		mddev->queue->backing_dev_info.congested_data = mddev;
		mddev->queue->backing_dev_info.congested_fn = raid5_congested;

		chunk_size = mddev->chunk_sectors << 9;
		blk_queue_io_min(mddev->queue, chunk_size);
		blk_queue_io_opt(mddev->queue, chunk_size *
				 (conf->raid_disks - conf->max_degraded));
		/*
		 * We can only discard a whole stripe. It doesn't make sense to
		 * discard data disk but write parity disk
		 */
		stripe = stripe * PAGE_SIZE;
		/* Round up to power of 2, as discard handling
		 * currently assumes that */
		while ((stripe-1) & stripe)
			stripe = (stripe | (stripe-1)) + 1;
		mddev->queue->limits.discard_alignment = stripe;
		mddev->queue->limits.discard_granularity = stripe;
		/*
		 * unaligned part of discard request will be ignored, so can't
		 * guarantee discard_zerors_data
		 */
		mddev->queue->limits.discard_zeroes_data = 0;

		blk_queue_max_write_same_sectors(mddev->queue, 0);

		rdev_for_each(rdev, mddev) {
			disk_stack_limits(mddev->gendisk, rdev->bdev,
					  rdev->data_offset << 9);
			disk_stack_limits(mddev->gendisk, rdev->bdev,
					  rdev->new_data_offset << 9);
			/*
			 * discard_zeroes_data is required, otherwise data
			 * could be lost. Consider a scenario: discard a stripe
			 * (the stripe could be inconsistent if
			 * discard_zeroes_data is 0); write one disk of the
			 * stripe (the stripe could be inconsistent again
			 * depending on which disks are used to calculate
			 * parity); the disk is broken; The stripe data of this
			 * disk is lost.
			 */
			if (!blk_queue_discard(bdev_get_queue(rdev->bdev)) ||
			    !bdev_get_queue(rdev->bdev)->
						limits.discard_zeroes_data)
				discard_supported = false;
		}

		if (discard_supported &&
		   mddev->queue->limits.max_discard_sectors >= stripe &&
		   mddev->queue->limits.discard_granularity >= stripe)
			queue_flag_set_unlocked(QUEUE_FLAG_DISCARD,
						mddev->queue);
		else
			queue_flag_clear_unlocked(QUEUE_FLAG_DISCARD,
						mddev->queue);
	}

	return 0;
abort:
	md_unregister_thread(&mddev->thread);
	print_raid5_conf(conf);
	free_conf(conf);
	mddev->private = NULL;
	printk(KERN_ALERT "md/raid:%s: failed to run raid set.\n", mdname(mddev));
	return -EIO;
}

static int stop(struct mddev *mddev)
{
	struct r5conf *conf = mddev->private;

	md_unregister_thread(&mddev->thread);
	if (mddev->queue)
		mddev->queue->backing_dev_info.congested_fn = NULL;
	free_conf(conf);
	mddev->private = NULL;
	mddev->to_remove = &raid5_attrs_group;
	return 0;
}

static void status(struct seq_file *seq, struct mddev *mddev)
{
	struct r5conf *conf = mddev->private;
	int i;

	seq_printf(seq, " level %d, %dk chunk, algorithm %d", mddev->level,
		mddev->chunk_sectors / 2, mddev->layout);
	seq_printf (seq, " [%d/%d] [", conf->raid_disks, conf->raid_disks - mddev->degraded);
	for (i = 0; i < conf->raid_disks; i++)
		seq_printf (seq, "%s",
			       conf->disks[i].rdev &&
			       test_bit(In_sync, &conf->disks[i].rdev->flags) ? "U" : "_");
	seq_printf (seq, "]");
}

static void print_raid5_conf (struct r5conf *conf)
{
	int i;
	struct disk_info *tmp;

	printk(KERN_DEBUG "RAID conf printout:\n");
	if (!conf) {
		printk("(conf==NULL)\n");
		return;
	}
	printk(KERN_DEBUG " --- level:%d rd:%d wd:%d\n", conf->level,
	       conf->raid_disks,
	       conf->raid_disks - conf->mddev->degraded);

	for (i = 0; i < conf->raid_disks; i++) {
		char b[BDEVNAME_SIZE];
		tmp = conf->disks + i;
		if (tmp->rdev)
			printk(KERN_DEBUG " disk %d, o:%d, dev:%s\n",
			       i, !test_bit(Faulty, &tmp->rdev->flags),
			       bdevname(tmp->rdev->bdev, b));
	}
}

static int raid5_spare_active(struct mddev *mddev)
{
	int i;
	struct r5conf *conf = mddev->private;
	struct disk_info *tmp;
	int count = 0;
	unsigned long flags;

	for (i = 0; i < conf->raid_disks; i++) {
		tmp = conf->disks + i;
		if (tmp->replacement
		    && tmp->replacement->recovery_offset == MaxSector
		    && !test_bit(Faulty, &tmp->replacement->flags)
		    && !test_and_set_bit(In_sync, &tmp->replacement->flags)) {
			/* Replacement has just become active. */
			if (!tmp->rdev
			    || !test_and_clear_bit(In_sync, &tmp->rdev->flags))
				count++;
			if (tmp->rdev) {
				/* Replaced device not technically faulty,
				 * but we need to be sure it gets removed
				 * and never re-added.
				 */
				set_bit(Faulty, &tmp->rdev->flags);
				sysfs_notify_dirent_safe(
					tmp->rdev->sysfs_state);
			}
			sysfs_notify_dirent_safe(tmp->replacement->sysfs_state);
		} else if (tmp->rdev
		    && tmp->rdev->recovery_offset == MaxSector
		    && !test_bit(Faulty, &tmp->rdev->flags)
		    && !test_and_set_bit(In_sync, &tmp->rdev->flags)) {
			count++;
			sysfs_notify_dirent_safe(tmp->rdev->sysfs_state);
		}
	}
	spin_lock_irqsave(&conf->device_lock, flags);
	mddev->degraded = calc_degraded(conf);
	spin_unlock_irqrestore(&conf->device_lock, flags);
	print_raid5_conf(conf);
	return count;
}

static int raid5_remove_disk(struct mddev *mddev, struct md_rdev *rdev)
{
	struct r5conf *conf = mddev->private;
	int err = 0;
	int number = rdev->raid_disk;
	struct md_rdev **rdevp;
	struct disk_info *p = conf->disks + number;

	print_raid5_conf(conf);
	if (rdev == p->rdev)
		rdevp = &p->rdev;
	else if (rdev == p->replacement)
		rdevp = &p->replacement;
	else
		return 0;

	if (number >= conf->raid_disks &&
	    conf->reshape_progress == MaxSector)
		clear_bit(In_sync, &rdev->flags);

	if (test_bit(In_sync, &rdev->flags) ||
	    atomic_read(&rdev->nr_pending)) {
		err = -EBUSY;
		goto abort;
	}
	/* Only remove non-faulty devices if recovery
	 * isn't possible.
	 */
	if (!test_bit(Faulty, &rdev->flags) &&
	    mddev->recovery_disabled != conf->recovery_disabled &&
	    !has_failed(conf) &&
	    (!p->replacement || p->replacement == rdev) &&
	    number < conf->raid_disks) {
		err = -EBUSY;
		goto abort;
	}
	*rdevp = NULL;
	synchronize_rcu();
	if (atomic_read(&rdev->nr_pending)) {
		/* lost the race, try later */
		err = -EBUSY;
		*rdevp = rdev;
	} else if (p->replacement) {
		/* We must have just cleared 'rdev' */
		p->rdev = p->replacement;
		clear_bit(Replacement, &p->replacement->flags);
		smp_mb(); /* Make sure other CPUs may see both as identical
			   * but will never see neither - if they are careful
			   */
		p->replacement = NULL;
		clear_bit(WantReplacement, &rdev->flags);
	} else
		/* We might have just removed the Replacement as faulty-
		 * clear the bit just in case
		 */
		clear_bit(WantReplacement, &rdev->flags);
abort:

	print_raid5_conf(conf);
	return err;
}

static int raid5_add_disk(struct mddev *mddev, struct md_rdev *rdev)
{
	struct r5conf *conf = mddev->private;
	int err = -EEXIST;
	int disk;
	struct disk_info *p;
	int first = 0;
	int last = conf->raid_disks - 1;

	if (mddev->recovery_disabled == conf->recovery_disabled)
		return -EBUSY;

	if (rdev->saved_raid_disk < 0 && has_failed(conf))
		/* no point adding a device */
		return -EINVAL;

	if (rdev->raid_disk >= 0)
		first = last = rdev->raid_disk;

	/*
	 * find the disk ... but prefer rdev->saved_raid_disk
	 * if possible.
	 */
	if (rdev->saved_raid_disk >= 0 &&
	    rdev->saved_raid_disk >= first &&
	    conf->disks[rdev->saved_raid_disk].rdev == NULL)
		first = rdev->saved_raid_disk;

	for (disk = first; disk <= last; disk++) {
		p = conf->disks + disk;
		if (p->rdev == NULL) {
			clear_bit(In_sync, &rdev->flags);
			rdev->raid_disk = disk;
			err = 0;
			if (rdev->saved_raid_disk != disk)
				conf->fullsync = 1;
			rcu_assign_pointer(p->rdev, rdev);
			goto out;
		}
	}
	for (disk = first; disk <= last; disk++) {
		p = conf->disks + disk;
		if (test_bit(WantReplacement, &p->rdev->flags) &&
		    p->replacement == NULL) {
			clear_bit(In_sync, &rdev->flags);
			set_bit(Replacement, &rdev->flags);
			rdev->raid_disk = disk;
			err = 0;
			conf->fullsync = 1;
			rcu_assign_pointer(p->replacement, rdev);
			break;
		}
	}
out:
	print_raid5_conf(conf);
	return err;
}

static int raid5_resize(struct mddev *mddev, sector_t sectors)
{
	/* no resync is happening, and there is enough space
	 * on all devices, so we can resize.
	 * We need to make sure resync covers any new space.
	 * If the array is shrinking we should possibly wait until
	 * any io in the removed space completes, but it hardly seems
	 * worth it.
	 */
	sector_t newsize;
	sectors &= ~((sector_t)mddev->chunk_sectors - 1);
	newsize = raid5_size(mddev, sectors, mddev->raid_disks);
	if (mddev->external_size &&
	    mddev->array_sectors > newsize)
		return -EINVAL;
	if (mddev->bitmap) {
		int ret = bitmap_resize(mddev->bitmap, sectors, 0, 0);
		if (ret)
			return ret;
	}
	md_set_array_sectors(mddev, newsize);
	set_capacity(mddev->gendisk, mddev->array_sectors);
	revalidate_disk(mddev->gendisk);
	if (sectors > mddev->dev_sectors &&
	    mddev->recovery_cp > mddev->dev_sectors) {
		mddev->recovery_cp = mddev->dev_sectors;
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	}
	mddev->dev_sectors = sectors;
	mddev->resync_max_sectors = sectors;
	return 0;
}

static int check_stripe_cache(struct mddev *mddev)
{
	/* Can only proceed if there are plenty of stripe_heads.
	 * We need a minimum of one full stripe,, and for sensible progress
	 * it is best to have about 4 times that.
	 * If we require 4 times, then the default 256 4K stripe_heads will
	 * allow for chunk sizes up to 256K, which is probably OK.
	 * If the chunk size is greater, user-space should request more
	 * stripe_heads first.
	 */
	struct r5conf *conf = mddev->private;
	if (((mddev->chunk_sectors << 9) / STRIPE_SIZE) * 4
	    > conf->max_nr_stripes ||
	    ((mddev->new_chunk_sectors << 9) / STRIPE_SIZE) * 4
	    > conf->max_nr_stripes) {
		printk(KERN_WARNING "md/raid:%s: reshape: not enough stripes.  Needed %lu\n",
		       mdname(mddev),
		       ((max(mddev->chunk_sectors, mddev->new_chunk_sectors) << 9)
			/ STRIPE_SIZE)*4);
		return 0;
	}
	return 1;
}

static int check_reshape(struct mddev *mddev)
{
	struct r5conf *conf = mddev->private;

	if (mddev->delta_disks == 0 &&
	    mddev->new_layout == mddev->layout &&
	    mddev->new_chunk_sectors == mddev->chunk_sectors)
		return 0; /* nothing to do */
	if (has_failed(conf))
		return -EINVAL;
	if (mddev->delta_disks < 0 && mddev->reshape_position == MaxSector) {
		/* We might be able to shrink, but the devices must
		 * be made bigger first.
		 * For raid6, 4 is the minimum size.
		 * Otherwise 2 is the minimum
		 */
		int min = 2;
		if (mddev->level == 6)
			min = 4;
		if (mddev->raid_disks + mddev->delta_disks < min)
			return -EINVAL;
	}

	if (!check_stripe_cache(mddev))
		return -ENOSPC;

	return resize_stripes(conf, (conf->previous_raid_disks
				     + mddev->delta_disks));
}

static int raid5_start_reshape(struct mddev *mddev)
{
	struct r5conf *conf = mddev->private;
	struct md_rdev *rdev;
	int spares = 0;
	unsigned long flags;

	if (test_bit(MD_RECOVERY_RUNNING, &mddev->recovery))
		return -EBUSY;

	if (!check_stripe_cache(mddev))
		return -ENOSPC;

	if (has_failed(conf))
		return -EINVAL;

	rdev_for_each(rdev, mddev) {
		if (!test_bit(In_sync, &rdev->flags)
		    && !test_bit(Faulty, &rdev->flags))
			spares++;
	}

	if (spares - mddev->degraded < mddev->delta_disks - conf->max_degraded)
		/* Not enough devices even to make a degraded array
		 * of that size
		 */
		return -EINVAL;

	/* Refuse to reduce size of the array.  Any reductions in
	 * array size must be through explicit setting of array_size
	 * attribute.
	 */
	if (raid5_size(mddev, 0, conf->raid_disks + mddev->delta_disks)
	    < mddev->array_sectors) {
		printk(KERN_ERR "md/raid:%s: array size must be reduced "
		       "before number of disks\n", mdname(mddev));
		return -EINVAL;
	}

	atomic_set(&conf->reshape_stripes, 0);
	spin_lock_irq(&conf->device_lock);
	write_seqcount_begin(&conf->gen_lock);
	conf->previous_raid_disks = conf->raid_disks;
	conf->raid_disks += mddev->delta_disks;
	conf->prev_chunk_sectors = conf->chunk_sectors;
	conf->chunk_sectors = mddev->new_chunk_sectors;
	conf->prev_algo = conf->algorithm;
	conf->algorithm = mddev->new_layout;
	conf->generation++;
	/* Code that selects data_offset needs to see the generation update
	 * if reshape_progress has been set - so a memory barrier needed.
	 */
	smp_mb();
	if (mddev->reshape_backwards)
		conf->reshape_progress = raid5_size(mddev, 0, 0);
	else
		conf->reshape_progress = 0;
	conf->reshape_safe = conf->reshape_progress;
	write_seqcount_end(&conf->gen_lock);
	spin_unlock_irq(&conf->device_lock);

	/* Now make sure any requests that proceeded on the assumption
	 * the reshape wasn't running - like Discard or Read - have
	 * completed.
	 */
	mddev_suspend(mddev);
	mddev_resume(mddev);

	/* Add some new drives, as many as will fit.
	 * We know there are enough to make the newly sized array work.
	 * Don't add devices if we are reducing the number of
	 * devices in the array.  This is because it is not possible
	 * to correctly record the "partially reconstructed" state of
	 * such devices during the reshape and confusion could result.
	 */
	if (mddev->delta_disks >= 0) {
		rdev_for_each(rdev, mddev)
			if (rdev->raid_disk < 0 &&
			    !test_bit(Faulty, &rdev->flags)) {
				if (raid5_add_disk(mddev, rdev) == 0) {
					if (rdev->raid_disk
					    >= conf->previous_raid_disks)
						set_bit(In_sync, &rdev->flags);
					else
						rdev->recovery_offset = 0;

					if (sysfs_link_rdev(mddev, rdev))
						/* Failure here is OK */;
				}
			} else if (rdev->raid_disk >= conf->previous_raid_disks
				   && !test_bit(Faulty, &rdev->flags)) {
				/* This is a spare that was manually added */
				set_bit(In_sync, &rdev->flags);
			}

		/* When a reshape changes the number of devices,
		 * ->degraded is measured against the larger of the
		 * pre and post number of devices.
		 */
		spin_lock_irqsave(&conf->device_lock, flags);
		mddev->degraded = calc_degraded(conf);
		spin_unlock_irqrestore(&conf->device_lock, flags);
	}
	mddev->raid_disks = conf->raid_disks;
	mddev->reshape_position = conf->reshape_progress;
	set_bit(MD_CHANGE_DEVS, &mddev->flags);

	clear_bit(MD_RECOVERY_SYNC, &mddev->recovery);
	clear_bit(MD_RECOVERY_CHECK, &mddev->recovery);
	set_bit(MD_RECOVERY_RESHAPE, &mddev->recovery);
	set_bit(MD_RECOVERY_RUNNING, &mddev->recovery);
	mddev->sync_thread = md_register_thread(md_do_sync, mddev,
						"reshape");
	if (!mddev->sync_thread) {
		mddev->recovery = 0;
		spin_lock_irq(&conf->device_lock);
		mddev->raid_disks = conf->raid_disks = conf->previous_raid_disks;
		rdev_for_each(rdev, mddev)
			rdev->new_data_offset = rdev->data_offset;
		smp_wmb();
		conf->reshape_progress = MaxSector;
		mddev->reshape_position = MaxSector;
		spin_unlock_irq(&conf->device_lock);
		return -EAGAIN;
	}
	conf->reshape_checkpoint = jiffies;
	md_wakeup_thread(mddev->sync_thread);
	md_new_event(mddev);
	return 0;
}

/* This is called from the reshape thread and should make any
 * changes needed in 'conf'
 */
static void end_reshape(struct r5conf *conf)
{

	if (!test_bit(MD_RECOVERY_INTR, &conf->mddev->recovery)) {
		struct md_rdev *rdev;

		spin_lock_irq(&conf->device_lock);
		conf->previous_raid_disks = conf->raid_disks;
		rdev_for_each(rdev, conf->mddev)
			rdev->data_offset = rdev->new_data_offset;
		smp_wmb();
		conf->reshape_progress = MaxSector;
		spin_unlock_irq(&conf->device_lock);
		wake_up(&conf->wait_for_overlap);

		/* read-ahead size must cover two whole stripes, which is
		 * 2 * (datadisks) * chunksize where 'n' is the number of raid devices
		 */
		if (conf->mddev->queue) {
			int data_disks = conf->raid_disks - conf->max_degraded;
			int stripe = data_disks * ((conf->chunk_sectors << 9)
						   / PAGE_SIZE);
			if (conf->mddev->queue->backing_dev_info.ra_pages < 2 * stripe)
				conf->mddev->queue->backing_dev_info.ra_pages = 2 * stripe;
		}
	}
}

/* This is called from the raid5d thread with mddev_lock held.
 * It makes config changes to the device.
 */
static void raid5_finish_reshape(struct mddev *mddev)
{
	struct r5conf *conf = mddev->private;

	if (!test_bit(MD_RECOVERY_INTR, &mddev->recovery)) {

		if (mddev->delta_disks > 0) {
			md_set_array_sectors(mddev, raid5_size(mddev, 0, 0));
			set_capacity(mddev->gendisk, mddev->array_sectors);
			revalidate_disk(mddev->gendisk);
		} else {
			int d;
			spin_lock_irq(&conf->device_lock);
			mddev->degraded = calc_degraded(conf);
			spin_unlock_irq(&conf->device_lock);
			for (d = conf->raid_disks ;
			     d < conf->raid_disks - mddev->delta_disks;
			     d++) {
				struct md_rdev *rdev = conf->disks[d].rdev;
				if (rdev)
					clear_bit(In_sync, &rdev->flags);
				rdev = conf->disks[d].replacement;
				if (rdev)
					clear_bit(In_sync, &rdev->flags);
			}
		}
		mddev->layout = conf->algorithm;
		mddev->chunk_sectors = conf->chunk_sectors;
		mddev->reshape_position = MaxSector;
		mddev->delta_disks = 0;
		mddev->reshape_backwards = 0;
	}
}

static void raid5_quiesce(struct mddev *mddev, int state)
{
	struct r5conf *conf = mddev->private;

	switch(state) {
	case 2: /* resume for a suspend */
		wake_up(&conf->wait_for_overlap);
		break;

	case 1: /* stop all writes */
		spin_lock_irq(&conf->device_lock);
		/* '2' tells resync/reshape to pause so that all
		 * active stripes can drain
		 */
		conf->quiesce = 2;
		wait_event_lock_irq(conf->wait_for_stripe,
				    atomic_read(&conf->active_stripes) == 0 &&
				    atomic_read(&conf->active_aligned_reads) == 0,
				    conf->device_lock);
		conf->quiesce = 1;
		spin_unlock_irq(&conf->device_lock);
		/* allow reshape to continue */
		wake_up(&conf->wait_for_overlap);
		break;

	case 0: /* re-enable writes */
		spin_lock_irq(&conf->device_lock);
		conf->quiesce = 0;
		wake_up(&conf->wait_for_stripe);
		wake_up(&conf->wait_for_overlap);
		spin_unlock_irq(&conf->device_lock);
		break;
	}
}


static void *raid45_takeover_raid0(struct mddev *mddev, int level)
{
	struct r0conf *raid0_conf = mddev->private;
	sector_t sectors;

	/* for raid0 takeover only one zone is supported */
	if (raid0_conf->nr_strip_zones > 1) {
		printk(KERN_ERR "md/raid:%s: cannot takeover raid0 with more than one zone.\n",
		       mdname(mddev));
		return ERR_PTR(-EINVAL);
	}

	sectors = raid0_conf->strip_zone[0].zone_end;
	sector_div(sectors, raid0_conf->strip_zone[0].nb_dev);
	mddev->dev_sectors = sectors;
	mddev->new_level = level;
	mddev->new_layout = ALGORITHM_PARITY_N;
	mddev->new_chunk_sectors = mddev->chunk_sectors;
	mddev->raid_disks += 1;
	mddev->delta_disks = 1;
	/* make sure it will be not marked as dirty */
	mddev->recovery_cp = MaxSector;

	return setup_conf(mddev);
}


static void *raid5_takeover_raid1(struct mddev *mddev)
{
	int chunksect;

	if (mddev->raid_disks != 2 ||
	    mddev->degraded > 1)
		return ERR_PTR(-EINVAL);

	/* Should check if there are write-behind devices? */

	chunksect = 64*2; /* 64K by default */

	/* The array must be an exact multiple of chunksize */
	while (chunksect && (mddev->array_sectors & (chunksect-1)))
		chunksect >>= 1;

	if ((chunksect<<9) < STRIPE_SIZE)
		/* array size does not allow a suitable chunk size */
		return ERR_PTR(-EINVAL);

	mddev->new_level = 5;
	mddev->new_layout = ALGORITHM_LEFT_SYMMETRIC;
	mddev->new_chunk_sectors = chunksect;

	return setup_conf(mddev);
}

static void *raid5_takeover_raid6(struct mddev *mddev)
{
	int new_layout;

	switch (mddev->layout) {
	case ALGORITHM_LEFT_ASYMMETRIC_6:
		new_layout = ALGORITHM_LEFT_ASYMMETRIC;
		break;
	case ALGORITHM_RIGHT_ASYMMETRIC_6:
		new_layout = ALGORITHM_RIGHT_ASYMMETRIC;
		break;
	case ALGORITHM_LEFT_SYMMETRIC_6:
		new_layout = ALGORITHM_LEFT_SYMMETRIC;
		break;
	case ALGORITHM_RIGHT_SYMMETRIC_6:
		new_layout = ALGORITHM_RIGHT_SYMMETRIC;
		break;
	case ALGORITHM_PARITY_0_6:
		new_layout = ALGORITHM_PARITY_0;
		break;
	case ALGORITHM_PARITY_N:
		new_layout = ALGORITHM_PARITY_N;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}
	mddev->new_level = 5;
	mddev->new_layout = new_layout;
	mddev->delta_disks = -1;
	mddev->raid_disks -= 1;
	return setup_conf(mddev);
}


static int raid5_check_reshape(struct mddev *mddev)
{
	/* For a 2-drive array, the layout and chunk size can be changed
	 * immediately as not restriping is needed.
	 * For larger arrays we record the new value - after validation
	 * to be used by a reshape pass.
	 */
	struct r5conf *conf = mddev->private;
	int new_chunk = mddev->new_chunk_sectors;

	if (mddev->new_layout >= 0 && !algorithm_valid_raid5(mddev->new_layout))
		return -EINVAL;
	if (new_chunk > 0) {
		if (!is_power_of_2(new_chunk))
			return -EINVAL;
		if (new_chunk < (PAGE_SIZE>>9))
			return -EINVAL;
		if (mddev->array_sectors & (new_chunk-1))
			/* not factor of array size */
			return -EINVAL;
	}

	/* They look valid */

	if (mddev->raid_disks == 2) {
		/* can make the change immediately */
		if (mddev->new_layout >= 0) {
			conf->algorithm = mddev->new_layout;
			mddev->layout = mddev->new_layout;
		}
		if (new_chunk > 0) {
			conf->chunk_sectors = new_chunk ;
			mddev->chunk_sectors = new_chunk;
		}
		set_bit(MD_CHANGE_DEVS, &mddev->flags);
		md_wakeup_thread(mddev->thread);
	}
	return check_reshape(mddev);
}

static int raid6_check_reshape(struct mddev *mddev)
{
	int new_chunk = mddev->new_chunk_sectors;

	if (mddev->new_layout >= 0 && !algorithm_valid_raid6(mddev->new_layout))
		return -EINVAL;
	if (new_chunk > 0) {
		if (!is_power_of_2(new_chunk))
			return -EINVAL;
		if (new_chunk < (PAGE_SIZE >> 9))
			return -EINVAL;
		if (mddev->array_sectors & (new_chunk-1))
			/* not factor of array size */
			return -EINVAL;
	}

	/* They look valid */
	return check_reshape(mddev);
}

static void *raid5_takeover(struct mddev *mddev)
{
	/* raid5 can take over:
	 *  raid0 - if there is only one strip zone - make it a raid4 layout
	 *  raid1 - if there are two drives.  We need to know the chunk size
	 *  raid4 - trivial - just use a raid4 layout.
	 *  raid6 - Providing it is a *_6 layout
	 */
	if (mddev->level == 0)
		return raid45_takeover_raid0(mddev, 5);
	if (mddev->level == 1)
		return raid5_takeover_raid1(mddev);
	if (mddev->level == 4) {
		mddev->new_layout = ALGORITHM_PARITY_N;
		mddev->new_level = 5;
		return setup_conf(mddev);
	}
	if (mddev->level == 6)
		return raid5_takeover_raid6(mddev);

	return ERR_PTR(-EINVAL);
}

static void *raid4_takeover(struct mddev *mddev)
{
	/* raid4 can take over:
	 *  raid0 - if there is only one strip zone
	 *  raid5 - if layout is right
	 */
	if (mddev->level == 0)
		return raid45_takeover_raid0(mddev, 4);
	if (mddev->level == 5 &&
	    mddev->layout == ALGORITHM_PARITY_N) {
		mddev->new_layout = 0;
		mddev->new_level = 4;
		return setup_conf(mddev);
	}
	return ERR_PTR(-EINVAL);
}

static struct md_personality raid5_personality;

static void *raid6_takeover(struct mddev *mddev)
{
	/* Currently can only take over a raid5.  We map the
	 * personality to an equivalent raid6 personality
	 * with the Q block at the end.
	 */
	int new_layout;

	if (mddev->pers != &raid5_personality)
		return ERR_PTR(-EINVAL);
	if (mddev->degraded > 1)
		return ERR_PTR(-EINVAL);
	if (mddev->raid_disks > 253)
		return ERR_PTR(-EINVAL);
	if (mddev->raid_disks < 3)
		return ERR_PTR(-EINVAL);

	switch (mddev->layout) {
	case ALGORITHM_LEFT_ASYMMETRIC:
		new_layout = ALGORITHM_LEFT_ASYMMETRIC_6;
		break;
	case ALGORITHM_RIGHT_ASYMMETRIC:
		new_layout = ALGORITHM_RIGHT_ASYMMETRIC_6;
		break;
	case ALGORITHM_LEFT_SYMMETRIC:
		new_layout = ALGORITHM_LEFT_SYMMETRIC_6;
		break;
	case ALGORITHM_RIGHT_SYMMETRIC:
		new_layout = ALGORITHM_RIGHT_SYMMETRIC_6;
		break;
	case ALGORITHM_PARITY_0:
		new_layout = ALGORITHM_PARITY_0_6;
		break;
	case ALGORITHM_PARITY_N:
		new_layout = ALGORITHM_PARITY_N;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}
	mddev->new_level = 6;
	mddev->new_layout = new_layout;
	mddev->delta_disks = 1;
	mddev->raid_disks += 1;
	return setup_conf(mddev);
}


static struct md_personality raid6_personality =
{
	.name		= "raid6",
	.level		= 6,
	.owner		= THIS_MODULE,
	.make_request	= make_request,
	.run		= run,
	.stop		= stop,
	.status		= status,
	.error_handler	= error,
	.hot_add_disk	= raid5_add_disk,
	.hot_remove_disk= raid5_remove_disk,
	.spare_active	= raid5_spare_active,
	.sync_request	= sync_request,
	.resize		= raid5_resize,
	.size		= raid5_size,
	.check_reshape	= raid6_check_reshape,
	.start_reshape  = raid5_start_reshape,
	.finish_reshape = raid5_finish_reshape,
	.quiesce	= raid5_quiesce,
	.takeover	= raid6_takeover,
};
static struct md_personality raid5_personality =
{
	.name		= "raid5",
	.level		= 5,
	.owner		= THIS_MODULE,
	.make_request	= make_request,
	.run		= run,
	.stop		= stop,
	.status		= status,
	.error_handler	= error,
	.hot_add_disk	= raid5_add_disk,
	.hot_remove_disk= raid5_remove_disk,
	.spare_active	= raid5_spare_active,
	.sync_request	= sync_request,
	.resize		= raid5_resize,
	.size		= raid5_size,
	.check_reshape	= raid5_check_reshape,
	.start_reshape  = raid5_start_reshape,
	.finish_reshape = raid5_finish_reshape,
	.quiesce	= raid5_quiesce,
	.takeover	= raid5_takeover,
};

static struct md_personality raid4_personality =
{
	.name		= "raid4",
	.level		= 4,
	.owner		= THIS_MODULE,
	.make_request	= make_request,
	.run		= run,
	.stop		= stop,
	.status		= status,
	.error_handler	= error,
	.hot_add_disk	= raid5_add_disk,
	.hot_remove_disk= raid5_remove_disk,
	.spare_active	= raid5_spare_active,
	.sync_request	= sync_request,
	.resize		= raid5_resize,
	.size		= raid5_size,
	.check_reshape	= raid5_check_reshape,
	.start_reshape  = raid5_start_reshape,
	.finish_reshape = raid5_finish_reshape,
	.quiesce	= raid5_quiesce,
	.takeover	= raid4_takeover,
};

static int __init raid5_init(void)
{
	raid5_wq = alloc_workqueue("raid5wq",
		WQ_UNBOUND|WQ_MEM_RECLAIM|WQ_CPU_INTENSIVE|WQ_SYSFS, 0);
	if (!raid5_wq)
		return -ENOMEM;
	register_md_personality(&raid6_personality);
	register_md_personality(&raid5_personality);
	register_md_personality(&raid4_personality);
	return 0;
}

static void raid5_exit(void)
{
	unregister_md_personality(&raid6_personality);
	unregister_md_personality(&raid5_personality);
	unregister_md_personality(&raid4_personality);
	destroy_workqueue(raid5_wq);
}

module_init(raid5_init);
module_exit(raid5_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RAID4/5/6 (striping with parity) personality for MD");
MODULE_ALIAS("md-personality-4"); /* RAID5 */
MODULE_ALIAS("md-raid5");
MODULE_ALIAS("md-raid4");
MODULE_ALIAS("md-level-5");
MODULE_ALIAS("md-level-4");
MODULE_ALIAS("md-personality-8"); /* RAID6 */
MODULE_ALIAS("md-raid6");
MODULE_ALIAS("md-level-6");

/* This used to be two separate modules, they were: */
MODULE_ALIAS("raid5");
MODULE_ALIAS("raid6");
