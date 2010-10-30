/*
 * raid1.c : Multiple Devices driver for Linux
 *
 * Copyright (C) 1999, 2000, 2001 Ingo Molnar, Red Hat
 *
 * Copyright (C) 1996, 1997, 1998 Ingo Molnar, Miguel de Icaza, Gadi Oxman
 *
 * RAID-1 management functions.
 *
 * Better read-balancing code written by Mika Kuoppala <miku@iki.fi>, 2000
 *
 * Fixes to reconstruction by Jakob Østergaard" <jakob@ostenfeld.dk>
 * Various fixes by Neil Brown <neilb@cse.unsw.edu.au>
 *
 * Changes by Peter T. Breuer <ptb@it.uc3m.es> 31/1/2003 to support
 * bitmapped intelligence in resync:
 *
 *      - bitmap marked during normal i/o
 *      - bitmap used to skip nondirty blocks during sync
 *
 * Additions to bitmap code, (C) 2003-2004 Paul Clements, SteelEye Technology:
 * - persistent bitmap code
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

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/seq_file.h>
#include "md.h"
#include "raid1.h"
#include "bitmap.h"

#define DEBUG 0
#if DEBUG
#define PRINTK(x...) printk(x)
#else
#define PRINTK(x...)
#endif

/*
 * Number of guaranteed r1bios in case of extreme VM load:
 */
#define	NR_RAID1_BIOS 256


static void unplug_slaves(mddev_t *mddev);

static void allow_barrier(conf_t *conf);
static void lower_barrier(conf_t *conf);

static void * r1bio_pool_alloc(gfp_t gfp_flags, void *data)
{
	struct pool_info *pi = data;
	r1bio_t *r1_bio;
	int size = offsetof(r1bio_t, bios[pi->raid_disks]);

	/* allocate a r1bio with room for raid_disks entries in the bios array */
	r1_bio = kzalloc(size, gfp_flags);
	if (!r1_bio && pi->mddev)
		unplug_slaves(pi->mddev);

	return r1_bio;
}

static void r1bio_pool_free(void *r1_bio, void *data)
{
	kfree(r1_bio);
}

#define RESYNC_BLOCK_SIZE (64*1024)
//#define RESYNC_BLOCK_SIZE PAGE_SIZE
#define RESYNC_SECTORS (RESYNC_BLOCK_SIZE >> 9)
#define RESYNC_PAGES ((RESYNC_BLOCK_SIZE + PAGE_SIZE-1) / PAGE_SIZE)
#define RESYNC_WINDOW (2048*1024)

static void * r1buf_pool_alloc(gfp_t gfp_flags, void *data)
{
	struct pool_info *pi = data;
	struct page *page;
	r1bio_t *r1_bio;
	struct bio *bio;
	int i, j;

	r1_bio = r1bio_pool_alloc(gfp_flags, pi);
	if (!r1_bio) {
		unplug_slaves(pi->mddev);
		return NULL;
	}

	/*
	 * Allocate bios : 1 for reading, n-1 for writing
	 */
	for (j = pi->raid_disks ; j-- ; ) {
		bio = bio_kmalloc(gfp_flags, RESYNC_PAGES);
		if (!bio)
			goto out_free_bio;
		r1_bio->bios[j] = bio;
	}
	/*
	 * Allocate RESYNC_PAGES data pages and attach them to
	 * the first bio.
	 * If this is a user-requested check/repair, allocate
	 * RESYNC_PAGES for each bio.
	 */
	if (test_bit(MD_RECOVERY_REQUESTED, &pi->mddev->recovery))
		j = pi->raid_disks;
	else
		j = 1;
	while(j--) {
		bio = r1_bio->bios[j];
		for (i = 0; i < RESYNC_PAGES; i++) {
			page = alloc_page(gfp_flags);
			if (unlikely(!page))
				goto out_free_pages;

			bio->bi_io_vec[i].bv_page = page;
			bio->bi_vcnt = i+1;
		}
	}
	/* If not user-requests, copy the page pointers to all bios */
	if (!test_bit(MD_RECOVERY_REQUESTED, &pi->mddev->recovery)) {
		for (i=0; i<RESYNC_PAGES ; i++)
			for (j=1; j<pi->raid_disks; j++)
				r1_bio->bios[j]->bi_io_vec[i].bv_page =
					r1_bio->bios[0]->bi_io_vec[i].bv_page;
	}

	r1_bio->master_bio = NULL;

	return r1_bio;

out_free_pages:
	for (j=0 ; j < pi->raid_disks; j++)
		for (i=0; i < r1_bio->bios[j]->bi_vcnt ; i++)
			put_page(r1_bio->bios[j]->bi_io_vec[i].bv_page);
	j = -1;
out_free_bio:
	while ( ++j < pi->raid_disks )
		bio_put(r1_bio->bios[j]);
	r1bio_pool_free(r1_bio, data);
	return NULL;
}

static void r1buf_pool_free(void *__r1_bio, void *data)
{
	struct pool_info *pi = data;
	int i,j;
	r1bio_t *r1bio = __r1_bio;

	for (i = 0; i < RESYNC_PAGES; i++)
		for (j = pi->raid_disks; j-- ;) {
			if (j == 0 ||
			    r1bio->bios[j]->bi_io_vec[i].bv_page !=
			    r1bio->bios[0]->bi_io_vec[i].bv_page)
				safe_put_page(r1bio->bios[j]->bi_io_vec[i].bv_page);
		}
	for (i=0 ; i < pi->raid_disks; i++)
		bio_put(r1bio->bios[i]);

	r1bio_pool_free(r1bio, data);
}

static void put_all_bios(conf_t *conf, r1bio_t *r1_bio)
{
	int i;

	for (i = 0; i < conf->raid_disks; i++) {
		struct bio **bio = r1_bio->bios + i;
		if (*bio && *bio != IO_BLOCKED)
			bio_put(*bio);
		*bio = NULL;
	}
}

static void free_r1bio(r1bio_t *r1_bio)
{
	conf_t *conf = r1_bio->mddev->private;

	/*
	 * Wake up any possible resync thread that waits for the device
	 * to go idle.
	 */
	allow_barrier(conf);

	put_all_bios(conf, r1_bio);
	mempool_free(r1_bio, conf->r1bio_pool);
}

static void put_buf(r1bio_t *r1_bio)
{
	conf_t *conf = r1_bio->mddev->private;
	int i;

	for (i=0; i<conf->raid_disks; i++) {
		struct bio *bio = r1_bio->bios[i];
		if (bio->bi_end_io)
			rdev_dec_pending(conf->mirrors[i].rdev, r1_bio->mddev);
	}

	mempool_free(r1_bio, conf->r1buf_pool);

	lower_barrier(conf);
}

static void reschedule_retry(r1bio_t *r1_bio)
{
	unsigned long flags;
	mddev_t *mddev = r1_bio->mddev;
	conf_t *conf = mddev->private;

	spin_lock_irqsave(&conf->device_lock, flags);
	list_add(&r1_bio->retry_list, &conf->retry_list);
	conf->nr_queued ++;
	spin_unlock_irqrestore(&conf->device_lock, flags);

	wake_up(&conf->wait_barrier);
	md_wakeup_thread(mddev->thread);
}

/*
 * raid_end_bio_io() is called when we have finished servicing a mirrored
 * operation and are ready to return a success/failure code to the buffer
 * cache layer.
 */
static void raid_end_bio_io(r1bio_t *r1_bio)
{
	struct bio *bio = r1_bio->master_bio;

	/* if nobody has done the final endio yet, do it now */
	if (!test_and_set_bit(R1BIO_Returned, &r1_bio->state)) {
		PRINTK(KERN_DEBUG "raid1: sync end %s on sectors %llu-%llu\n",
			(bio_data_dir(bio) == WRITE) ? "write" : "read",
			(unsigned long long) bio->bi_sector,
			(unsigned long long) bio->bi_sector +
				(bio->bi_size >> 9) - 1);

		bio_endio(bio,
			test_bit(R1BIO_Uptodate, &r1_bio->state) ? 0 : -EIO);
	}
	free_r1bio(r1_bio);
}

/*
 * Update disk head position estimator based on IRQ completion info.
 */
static inline void update_head_pos(int disk, r1bio_t *r1_bio)
{
	conf_t *conf = r1_bio->mddev->private;

	conf->mirrors[disk].head_position =
		r1_bio->sector + (r1_bio->sectors);
}

static void raid1_end_read_request(struct bio *bio, int error)
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	r1bio_t *r1_bio = bio->bi_private;
	int mirror;
	conf_t *conf = r1_bio->mddev->private;

	mirror = r1_bio->read_disk;
	/*
	 * this branch is our 'one mirror IO has finished' event handler:
	 */
	update_head_pos(mirror, r1_bio);

	if (uptodate)
		set_bit(R1BIO_Uptodate, &r1_bio->state);
	else {
		/* If all other devices have failed, we want to return
		 * the error upwards rather than fail the last device.
		 * Here we redefine "uptodate" to mean "Don't want to retry"
		 */
		unsigned long flags;
		spin_lock_irqsave(&conf->device_lock, flags);
		if (r1_bio->mddev->degraded == conf->raid_disks ||
		    (r1_bio->mddev->degraded == conf->raid_disks-1 &&
		     !test_bit(Faulty, &conf->mirrors[mirror].rdev->flags)))
			uptodate = 1;
		spin_unlock_irqrestore(&conf->device_lock, flags);
	}

	if (uptodate)
		raid_end_bio_io(r1_bio);
	else {
		/*
		 * oops, read error:
		 */
		char b[BDEVNAME_SIZE];
		if (printk_ratelimit())
			printk(KERN_ERR "md/raid1:%s: %s: rescheduling sector %llu\n",
			       mdname(conf->mddev),
			       bdevname(conf->mirrors[mirror].rdev->bdev,b), (unsigned long long)r1_bio->sector);
		reschedule_retry(r1_bio);
	}

	rdev_dec_pending(conf->mirrors[mirror].rdev, conf->mddev);
}

static void r1_bio_write_done(r1bio_t *r1_bio, int vcnt, struct bio_vec *bv,
			      int behind)
{
	if (atomic_dec_and_test(&r1_bio->remaining))
	{
		/* it really is the end of this request */
		if (test_bit(R1BIO_BehindIO, &r1_bio->state)) {
			/* free extra copy of the data pages */
			int i = vcnt;
			while (i--)
				safe_put_page(bv[i].bv_page);
		}
		/* clear the bitmap if all writes complete successfully */
		bitmap_endwrite(r1_bio->mddev->bitmap, r1_bio->sector,
				r1_bio->sectors,
				!test_bit(R1BIO_Degraded, &r1_bio->state),
				behind);
		md_write_end(r1_bio->mddev);
		raid_end_bio_io(r1_bio);
	}
}

static void raid1_end_write_request(struct bio *bio, int error)
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	r1bio_t *r1_bio = bio->bi_private;
	int mirror, behind = test_bit(R1BIO_BehindIO, &r1_bio->state);
	conf_t *conf = r1_bio->mddev->private;
	struct bio *to_put = NULL;


	for (mirror = 0; mirror < conf->raid_disks; mirror++)
		if (r1_bio->bios[mirror] == bio)
			break;

	/*
	 * 'one mirror IO has finished' event handler:
	 */
	r1_bio->bios[mirror] = NULL;
	to_put = bio;
	if (!uptodate) {
		md_error(r1_bio->mddev, conf->mirrors[mirror].rdev);
		/* an I/O failed, we can't clear the bitmap */
		set_bit(R1BIO_Degraded, &r1_bio->state);
	} else
		/*
		 * Set R1BIO_Uptodate in our master bio, so that we
		 * will return a good error code for to the higher
		 * levels even if IO on some other mirrored buffer
		 * fails.
		 *
		 * The 'master' represents the composite IO operation
		 * to user-side. So if something waits for IO, then it
		 * will wait for the 'master' bio.
		 */
		set_bit(R1BIO_Uptodate, &r1_bio->state);

	update_head_pos(mirror, r1_bio);

	if (behind) {
		if (test_bit(WriteMostly, &conf->mirrors[mirror].rdev->flags))
			atomic_dec(&r1_bio->behind_remaining);

		/*
		 * In behind mode, we ACK the master bio once the I/O
		 * has safely reached all non-writemostly
		 * disks. Setting the Returned bit ensures that this
		 * gets done only once -- we don't ever want to return
		 * -EIO here, instead we'll wait
		 */
		if (atomic_read(&r1_bio->behind_remaining) >= (atomic_read(&r1_bio->remaining)-1) &&
		    test_bit(R1BIO_Uptodate, &r1_bio->state)) {
			/* Maybe we can return now */
			if (!test_and_set_bit(R1BIO_Returned, &r1_bio->state)) {
				struct bio *mbio = r1_bio->master_bio;
				PRINTK(KERN_DEBUG "raid1: behind end write sectors %llu-%llu\n",
				       (unsigned long long) mbio->bi_sector,
				       (unsigned long long) mbio->bi_sector +
				       (mbio->bi_size >> 9) - 1);
				bio_endio(mbio, 0);
			}
		}
	}
	rdev_dec_pending(conf->mirrors[mirror].rdev, conf->mddev);

	/*
	 * Let's see if all mirrored write operations have finished
	 * already.
	 */
	r1_bio_write_done(r1_bio, bio->bi_vcnt, bio->bi_io_vec, behind);

	if (to_put)
		bio_put(to_put);
}


/*
 * This routine returns the disk from which the requested read should
 * be done. There is a per-array 'next expected sequential IO' sector
 * number - if this matches on the next IO then we use the last disk.
 * There is also a per-disk 'last know head position' sector that is
 * maintained from IRQ contexts, both the normal and the resync IO
 * completion handlers update this position correctly. If there is no
 * perfect sequential match then we pick the disk whose head is closest.
 *
 * If there are 2 mirrors in the same 2 devices, performance degrades
 * because position is mirror, not device based.
 *
 * The rdev for the device selected will have nr_pending incremented.
 */
static int read_balance(conf_t *conf, r1bio_t *r1_bio)
{
	const sector_t this_sector = r1_bio->sector;
	const int sectors = r1_bio->sectors;
	int new_disk = -1;
	int start_disk;
	int i;
	sector_t new_distance, current_distance;
	mdk_rdev_t *rdev;
	int choose_first;

	rcu_read_lock();
	/*
	 * Check if we can balance. We can balance on the whole
	 * device if no resync is going on, or below the resync window.
	 * We take the first readable disk when above the resync window.
	 */
 retry:
	if (conf->mddev->recovery_cp < MaxSector &&
	    (this_sector + sectors >= conf->next_resync)) {
		choose_first = 1;
		start_disk = 0;
	} else {
		choose_first = 0;
		start_disk = conf->last_used;
	}

	/* make sure the disk is operational */
	for (i = 0 ; i < conf->raid_disks ; i++) {
		int disk = start_disk + i;
		if (disk >= conf->raid_disks)
			disk -= conf->raid_disks;

		rdev = rcu_dereference(conf->mirrors[disk].rdev);
		if (r1_bio->bios[disk] == IO_BLOCKED
		    || rdev == NULL
		    || !test_bit(In_sync, &rdev->flags))
			continue;

		new_disk = disk;
		if (!test_bit(WriteMostly, &rdev->flags))
			break;
	}

	if (new_disk < 0 || choose_first)
		goto rb_out;

	/*
	 * Don't change to another disk for sequential reads:
	 */
	if (conf->next_seq_sect == this_sector)
		goto rb_out;
	if (this_sector == conf->mirrors[new_disk].head_position)
		goto rb_out;

	current_distance = abs(this_sector 
			       - conf->mirrors[new_disk].head_position);

	/* look for a better disk - i.e. head is closer */
	start_disk = new_disk;
	for (i = 1; i < conf->raid_disks; i++) {
		int disk = start_disk + 1;
		if (disk >= conf->raid_disks)
			disk -= conf->raid_disks;

		rdev = rcu_dereference(conf->mirrors[disk].rdev);
		if (r1_bio->bios[disk] == IO_BLOCKED
		    || rdev == NULL
		    || !test_bit(In_sync, &rdev->flags)
		    || test_bit(WriteMostly, &rdev->flags))
			continue;

		if (!atomic_read(&rdev->nr_pending)) {
			new_disk = disk;
			break;
		}
		new_distance = abs(this_sector - conf->mirrors[disk].head_position);
		if (new_distance < current_distance) {
			current_distance = new_distance;
			new_disk = disk;
		}
	}

 rb_out:
	if (new_disk >= 0) {
		rdev = rcu_dereference(conf->mirrors[new_disk].rdev);
		if (!rdev)
			goto retry;
		atomic_inc(&rdev->nr_pending);
		if (!test_bit(In_sync, &rdev->flags)) {
			/* cannot risk returning a device that failed
			 * before we inc'ed nr_pending
			 */
			rdev_dec_pending(rdev, conf->mddev);
			goto retry;
		}
		conf->next_seq_sect = this_sector + sectors;
		conf->last_used = new_disk;
	}
	rcu_read_unlock();

	return new_disk;
}

static void unplug_slaves(mddev_t *mddev)
{
	conf_t *conf = mddev->private;
	int i;

	rcu_read_lock();
	for (i=0; i<mddev->raid_disks; i++) {
		mdk_rdev_t *rdev = rcu_dereference(conf->mirrors[i].rdev);
		if (rdev && !test_bit(Faulty, &rdev->flags) && atomic_read(&rdev->nr_pending)) {
			struct request_queue *r_queue = bdev_get_queue(rdev->bdev);

			atomic_inc(&rdev->nr_pending);
			rcu_read_unlock();

			blk_unplug(r_queue);

			rdev_dec_pending(rdev, mddev);
			rcu_read_lock();
		}
	}
	rcu_read_unlock();
}

static void raid1_unplug(struct request_queue *q)
{
	mddev_t *mddev = q->queuedata;

	unplug_slaves(mddev);
	md_wakeup_thread(mddev->thread);
}

static int raid1_congested(void *data, int bits)
{
	mddev_t *mddev = data;
	conf_t *conf = mddev->private;
	int i, ret = 0;

	if (mddev_congested(mddev, bits))
		return 1;

	rcu_read_lock();
	for (i = 0; i < mddev->raid_disks; i++) {
		mdk_rdev_t *rdev = rcu_dereference(conf->mirrors[i].rdev);
		if (rdev && !test_bit(Faulty, &rdev->flags)) {
			struct request_queue *q = bdev_get_queue(rdev->bdev);

			/* Note the '|| 1' - when read_balance prefers
			 * non-congested targets, it can be removed
			 */
			if ((bits & (1<<BDI_async_congested)) || 1)
				ret |= bdi_congested(&q->backing_dev_info, bits);
			else
				ret &= bdi_congested(&q->backing_dev_info, bits);
		}
	}
	rcu_read_unlock();
	return ret;
}


static int flush_pending_writes(conf_t *conf)
{
	/* Any writes that have been queued but are awaiting
	 * bitmap updates get flushed here.
	 * We return 1 if any requests were actually submitted.
	 */
	int rv = 0;

	spin_lock_irq(&conf->device_lock);

	if (conf->pending_bio_list.head) {
		struct bio *bio;
		bio = bio_list_get(&conf->pending_bio_list);
		blk_remove_plug(conf->mddev->queue);
		spin_unlock_irq(&conf->device_lock);
		/* flush any pending bitmap writes to
		 * disk before proceeding w/ I/O */
		bitmap_unplug(conf->mddev->bitmap);

		while (bio) { /* submit pending writes */
			struct bio *next = bio->bi_next;
			bio->bi_next = NULL;
			generic_make_request(bio);
			bio = next;
		}
		rv = 1;
	} else
		spin_unlock_irq(&conf->device_lock);
	return rv;
}

/* Barriers....
 * Sometimes we need to suspend IO while we do something else,
 * either some resync/recovery, or reconfigure the array.
 * To do this we raise a 'barrier'.
 * The 'barrier' is a counter that can be raised multiple times
 * to count how many activities are happening which preclude
 * normal IO.
 * We can only raise the barrier if there is no pending IO.
 * i.e. if nr_pending == 0.
 * We choose only to raise the barrier if no-one is waiting for the
 * barrier to go down.  This means that as soon as an IO request
 * is ready, no other operations which require a barrier will start
 * until the IO request has had a chance.
 *
 * So: regular IO calls 'wait_barrier'.  When that returns there
 *    is no backgroup IO happening,  It must arrange to call
 *    allow_barrier when it has finished its IO.
 * backgroup IO calls must call raise_barrier.  Once that returns
 *    there is no normal IO happeing.  It must arrange to call
 *    lower_barrier when the particular background IO completes.
 */
#define RESYNC_DEPTH 32

static void raise_barrier(conf_t *conf)
{
	spin_lock_irq(&conf->resync_lock);

	/* Wait until no block IO is waiting */
	wait_event_lock_irq(conf->wait_barrier, !conf->nr_waiting,
			    conf->resync_lock,
			    raid1_unplug(conf->mddev->queue));

	/* block any new IO from starting */
	conf->barrier++;

	/* Now wait for all pending IO to complete */
	wait_event_lock_irq(conf->wait_barrier,
			    !conf->nr_pending && conf->barrier < RESYNC_DEPTH,
			    conf->resync_lock,
			    raid1_unplug(conf->mddev->queue));

	spin_unlock_irq(&conf->resync_lock);
}

static void lower_barrier(conf_t *conf)
{
	unsigned long flags;
	BUG_ON(conf->barrier <= 0);
	spin_lock_irqsave(&conf->resync_lock, flags);
	conf->barrier--;
	spin_unlock_irqrestore(&conf->resync_lock, flags);
	wake_up(&conf->wait_barrier);
}

static void wait_barrier(conf_t *conf)
{
	spin_lock_irq(&conf->resync_lock);
	if (conf->barrier) {
		conf->nr_waiting++;
		wait_event_lock_irq(conf->wait_barrier, !conf->barrier,
				    conf->resync_lock,
				    raid1_unplug(conf->mddev->queue));
		conf->nr_waiting--;
	}
	conf->nr_pending++;
	spin_unlock_irq(&conf->resync_lock);
}

static void allow_barrier(conf_t *conf)
{
	unsigned long flags;
	spin_lock_irqsave(&conf->resync_lock, flags);
	conf->nr_pending--;
	spin_unlock_irqrestore(&conf->resync_lock, flags);
	wake_up(&conf->wait_barrier);
}

static void freeze_array(conf_t *conf)
{
	/* stop syncio and normal IO and wait for everything to
	 * go quite.
	 * We increment barrier and nr_waiting, and then
	 * wait until nr_pending match nr_queued+1
	 * This is called in the context of one normal IO request
	 * that has failed. Thus any sync request that might be pending
	 * will be blocked by nr_pending, and we need to wait for
	 * pending IO requests to complete or be queued for re-try.
	 * Thus the number queued (nr_queued) plus this request (1)
	 * must match the number of pending IOs (nr_pending) before
	 * we continue.
	 */
	spin_lock_irq(&conf->resync_lock);
	conf->barrier++;
	conf->nr_waiting++;
	wait_event_lock_irq(conf->wait_barrier,
			    conf->nr_pending == conf->nr_queued+1,
			    conf->resync_lock,
			    ({ flush_pending_writes(conf);
			       raid1_unplug(conf->mddev->queue); }));
	spin_unlock_irq(&conf->resync_lock);
}
static void unfreeze_array(conf_t *conf)
{
	/* reverse the effect of the freeze */
	spin_lock_irq(&conf->resync_lock);
	conf->barrier--;
	conf->nr_waiting--;
	wake_up(&conf->wait_barrier);
	spin_unlock_irq(&conf->resync_lock);
}


/* duplicate the data pages for behind I/O 
 * We return a list of bio_vec rather than just page pointers
 * as it makes freeing easier
 */
static struct bio_vec *alloc_behind_pages(struct bio *bio)
{
	int i;
	struct bio_vec *bvec;
	struct bio_vec *pages = kzalloc(bio->bi_vcnt * sizeof(struct bio_vec),
					GFP_NOIO);
	if (unlikely(!pages))
		goto do_sync_io;

	bio_for_each_segment(bvec, bio, i) {
		pages[i].bv_page = alloc_page(GFP_NOIO);
		if (unlikely(!pages[i].bv_page))
			goto do_sync_io;
		memcpy(kmap(pages[i].bv_page) + bvec->bv_offset,
			kmap(bvec->bv_page) + bvec->bv_offset, bvec->bv_len);
		kunmap(pages[i].bv_page);
		kunmap(bvec->bv_page);
	}

	return pages;

do_sync_io:
	if (pages)
		for (i = 0; i < bio->bi_vcnt && pages[i].bv_page; i++)
			put_page(pages[i].bv_page);
	kfree(pages);
	PRINTK("%dB behind alloc failed, doing sync I/O\n", bio->bi_size);
	return NULL;
}

static int make_request(mddev_t *mddev, struct bio * bio)
{
	conf_t *conf = mddev->private;
	mirror_info_t *mirror;
	r1bio_t *r1_bio;
	struct bio *read_bio;
	int i, targets = 0, disks;
	struct bitmap *bitmap;
	unsigned long flags;
	struct bio_vec *behind_pages = NULL;
	const int rw = bio_data_dir(bio);
	const unsigned long do_sync = (bio->bi_rw & REQ_SYNC);
	const unsigned long do_flush_fua = (bio->bi_rw & (REQ_FLUSH | REQ_FUA));
	mdk_rdev_t *blocked_rdev;

	/*
	 * Register the new request and wait if the reconstruction
	 * thread has put up a bar for new requests.
	 * Continue immediately if no resync is active currently.
	 */

	md_write_start(mddev, bio); /* wait on superblock update early */

	if (bio_data_dir(bio) == WRITE &&
	    bio->bi_sector + bio->bi_size/512 > mddev->suspend_lo &&
	    bio->bi_sector < mddev->suspend_hi) {
		/* As the suspend_* range is controlled by
		 * userspace, we want an interruptible
		 * wait.
		 */
		DEFINE_WAIT(w);
		for (;;) {
			flush_signals(current);
			prepare_to_wait(&conf->wait_barrier,
					&w, TASK_INTERRUPTIBLE);
			if (bio->bi_sector + bio->bi_size/512 <= mddev->suspend_lo ||
			    bio->bi_sector >= mddev->suspend_hi)
				break;
			schedule();
		}
		finish_wait(&conf->wait_barrier, &w);
	}

	wait_barrier(conf);

	bitmap = mddev->bitmap;

	/*
	 * make_request() can abort the operation when READA is being
	 * used and no empty request is available.
	 *
	 */
	r1_bio = mempool_alloc(conf->r1bio_pool, GFP_NOIO);

	r1_bio->master_bio = bio;
	r1_bio->sectors = bio->bi_size >> 9;
	r1_bio->state = 0;
	r1_bio->mddev = mddev;
	r1_bio->sector = bio->bi_sector;

	if (rw == READ) {
		/*
		 * read balancing logic:
		 */
		int rdisk = read_balance(conf, r1_bio);

		if (rdisk < 0) {
			/* couldn't find anywhere to read from */
			raid_end_bio_io(r1_bio);
			return 0;
		}
		mirror = conf->mirrors + rdisk;

		if (test_bit(WriteMostly, &mirror->rdev->flags) &&
		    bitmap) {
			/* Reading from a write-mostly device must
			 * take care not to over-take any writes
			 * that are 'behind'
			 */
			wait_event(bitmap->behind_wait,
				   atomic_read(&bitmap->behind_writes) == 0);
		}
		r1_bio->read_disk = rdisk;

		read_bio = bio_clone_mddev(bio, GFP_NOIO, mddev);

		r1_bio->bios[rdisk] = read_bio;

		read_bio->bi_sector = r1_bio->sector + mirror->rdev->data_offset;
		read_bio->bi_bdev = mirror->rdev->bdev;
		read_bio->bi_end_io = raid1_end_read_request;
		read_bio->bi_rw = READ | do_sync;
		read_bio->bi_private = r1_bio;

		generic_make_request(read_bio);
		return 0;
	}

	/*
	 * WRITE:
	 */
	/* first select target devices under spinlock and
	 * inc refcount on their rdev.  Record them by setting
	 * bios[x] to bio
	 */
	disks = conf->raid_disks;
 retry_write:
	blocked_rdev = NULL;
	rcu_read_lock();
	for (i = 0;  i < disks; i++) {
		mdk_rdev_t *rdev = rcu_dereference(conf->mirrors[i].rdev);
		if (rdev && unlikely(test_bit(Blocked, &rdev->flags))) {
			atomic_inc(&rdev->nr_pending);
			blocked_rdev = rdev;
			break;
		}
		if (rdev && !test_bit(Faulty, &rdev->flags)) {
			atomic_inc(&rdev->nr_pending);
			if (test_bit(Faulty, &rdev->flags)) {
				rdev_dec_pending(rdev, mddev);
				r1_bio->bios[i] = NULL;
			} else {
				r1_bio->bios[i] = bio;
				targets++;
			}
		} else
			r1_bio->bios[i] = NULL;
	}
	rcu_read_unlock();

	if (unlikely(blocked_rdev)) {
		/* Wait for this device to become unblocked */
		int j;

		for (j = 0; j < i; j++)
			if (r1_bio->bios[j])
				rdev_dec_pending(conf->mirrors[j].rdev, mddev);

		allow_barrier(conf);
		md_wait_for_blocked_rdev(blocked_rdev, mddev);
		wait_barrier(conf);
		goto retry_write;
	}

	BUG_ON(targets == 0); /* we never fail the last device */

	if (targets < conf->raid_disks) {
		/* array is degraded, we will not clear the bitmap
		 * on I/O completion (see raid1_end_write_request) */
		set_bit(R1BIO_Degraded, &r1_bio->state);
	}

	/* do behind I/O ?
	 * Not if there are too many, or cannot allocate memory,
	 * or a reader on WriteMostly is waiting for behind writes 
	 * to flush */
	if (bitmap &&
	    (atomic_read(&bitmap->behind_writes)
	     < mddev->bitmap_info.max_write_behind) &&
	    !waitqueue_active(&bitmap->behind_wait) &&
	    (behind_pages = alloc_behind_pages(bio)) != NULL)
		set_bit(R1BIO_BehindIO, &r1_bio->state);

	atomic_set(&r1_bio->remaining, 1);
	atomic_set(&r1_bio->behind_remaining, 0);

	bitmap_startwrite(bitmap, bio->bi_sector, r1_bio->sectors,
				test_bit(R1BIO_BehindIO, &r1_bio->state));
	for (i = 0; i < disks; i++) {
		struct bio *mbio;
		if (!r1_bio->bios[i])
			continue;

		mbio = bio_clone_mddev(bio, GFP_NOIO, mddev);
		r1_bio->bios[i] = mbio;

		mbio->bi_sector	= r1_bio->sector + conf->mirrors[i].rdev->data_offset;
		mbio->bi_bdev = conf->mirrors[i].rdev->bdev;
		mbio->bi_end_io	= raid1_end_write_request;
		mbio->bi_rw = WRITE | do_flush_fua | do_sync;
		mbio->bi_private = r1_bio;

		if (behind_pages) {
			struct bio_vec *bvec;
			int j;

			/* Yes, I really want the '__' version so that
			 * we clear any unused pointer in the io_vec, rather
			 * than leave them unchanged.  This is important
			 * because when we come to free the pages, we won't
			 * know the original bi_idx, so we just free
			 * them all
			 */
			__bio_for_each_segment(bvec, mbio, j, 0)
				bvec->bv_page = behind_pages[j].bv_page;
			if (test_bit(WriteMostly, &conf->mirrors[i].rdev->flags))
				atomic_inc(&r1_bio->behind_remaining);
		}

		atomic_inc(&r1_bio->remaining);
		spin_lock_irqsave(&conf->device_lock, flags);
		bio_list_add(&conf->pending_bio_list, mbio);
		blk_plug_device(mddev->queue);
		spin_unlock_irqrestore(&conf->device_lock, flags);
	}
	r1_bio_write_done(r1_bio, bio->bi_vcnt, behind_pages, behind_pages != NULL);
	kfree(behind_pages); /* the behind pages are attached to the bios now */

	/* In case raid1d snuck in to freeze_array */
	wake_up(&conf->wait_barrier);

	if (do_sync)
		md_wakeup_thread(mddev->thread);

	return 0;
}

static void status(struct seq_file *seq, mddev_t *mddev)
{
	conf_t *conf = mddev->private;
	int i;

	seq_printf(seq, " [%d/%d] [", conf->raid_disks,
		   conf->raid_disks - mddev->degraded);
	rcu_read_lock();
	for (i = 0; i < conf->raid_disks; i++) {
		mdk_rdev_t *rdev = rcu_dereference(conf->mirrors[i].rdev);
		seq_printf(seq, "%s",
			   rdev && test_bit(In_sync, &rdev->flags) ? "U" : "_");
	}
	rcu_read_unlock();
	seq_printf(seq, "]");
}


static void error(mddev_t *mddev, mdk_rdev_t *rdev)
{
	char b[BDEVNAME_SIZE];
	conf_t *conf = mddev->private;

	/*
	 * If it is not operational, then we have already marked it as dead
	 * else if it is the last working disks, ignore the error, let the
	 * next level up know.
	 * else mark the drive as failed
	 */
	if (test_bit(In_sync, &rdev->flags)
	    && (conf->raid_disks - mddev->degraded) == 1) {
		/*
		 * Don't fail the drive, act as though we were just a
		 * normal single drive.
		 * However don't try a recovery from this drive as
		 * it is very likely to fail.
		 */
		mddev->recovery_disabled = 1;
		return;
	}
	if (test_and_clear_bit(In_sync, &rdev->flags)) {
		unsigned long flags;
		spin_lock_irqsave(&conf->device_lock, flags);
		mddev->degraded++;
		set_bit(Faulty, &rdev->flags);
		spin_unlock_irqrestore(&conf->device_lock, flags);
		/*
		 * if recovery is running, make sure it aborts.
		 */
		set_bit(MD_RECOVERY_INTR, &mddev->recovery);
	} else
		set_bit(Faulty, &rdev->flags);
	set_bit(MD_CHANGE_DEVS, &mddev->flags);
	printk(KERN_ALERT "md/raid1:%s: Disk failure on %s, disabling device.\n"
	       KERN_ALERT "md/raid1:%s: Operation continuing on %d devices.\n",
	       mdname(mddev), bdevname(rdev->bdev, b),
	       mdname(mddev), conf->raid_disks - mddev->degraded);
}

static void print_conf(conf_t *conf)
{
	int i;

	printk(KERN_DEBUG "RAID1 conf printout:\n");
	if (!conf) {
		printk(KERN_DEBUG "(!conf)\n");
		return;
	}
	printk(KERN_DEBUG " --- wd:%d rd:%d\n", conf->raid_disks - conf->mddev->degraded,
		conf->raid_disks);

	rcu_read_lock();
	for (i = 0; i < conf->raid_disks; i++) {
		char b[BDEVNAME_SIZE];
		mdk_rdev_t *rdev = rcu_dereference(conf->mirrors[i].rdev);
		if (rdev)
			printk(KERN_DEBUG " disk %d, wo:%d, o:%d, dev:%s\n",
			       i, !test_bit(In_sync, &rdev->flags),
			       !test_bit(Faulty, &rdev->flags),
			       bdevname(rdev->bdev,b));
	}
	rcu_read_unlock();
}

static void close_sync(conf_t *conf)
{
	wait_barrier(conf);
	allow_barrier(conf);

	mempool_destroy(conf->r1buf_pool);
	conf->r1buf_pool = NULL;
}

static int raid1_spare_active(mddev_t *mddev)
{
	int i;
	conf_t *conf = mddev->private;
	int count = 0;
	unsigned long flags;

	/*
	 * Find all failed disks within the RAID1 configuration 
	 * and mark them readable.
	 * Called under mddev lock, so rcu protection not needed.
	 */
	for (i = 0; i < conf->raid_disks; i++) {
		mdk_rdev_t *rdev = conf->mirrors[i].rdev;
		if (rdev
		    && !test_bit(Faulty, &rdev->flags)
		    && !test_and_set_bit(In_sync, &rdev->flags)) {
			count++;
			sysfs_notify_dirent(rdev->sysfs_state);
		}
	}
	spin_lock_irqsave(&conf->device_lock, flags);
	mddev->degraded -= count;
	spin_unlock_irqrestore(&conf->device_lock, flags);

	print_conf(conf);
	return count;
}


static int raid1_add_disk(mddev_t *mddev, mdk_rdev_t *rdev)
{
	conf_t *conf = mddev->private;
	int err = -EEXIST;
	int mirror = 0;
	mirror_info_t *p;
	int first = 0;
	int last = mddev->raid_disks - 1;

	if (rdev->raid_disk >= 0)
		first = last = rdev->raid_disk;

	for (mirror = first; mirror <= last; mirror++)
		if ( !(p=conf->mirrors+mirror)->rdev) {

			disk_stack_limits(mddev->gendisk, rdev->bdev,
					  rdev->data_offset << 9);
			/* as we don't honour merge_bvec_fn, we must
			 * never risk violating it, so limit
			 * ->max_segments to one lying with a single
			 * page, as a one page request is never in
			 * violation.
			 */
			if (rdev->bdev->bd_disk->queue->merge_bvec_fn) {
				blk_queue_max_segments(mddev->queue, 1);
				blk_queue_segment_boundary(mddev->queue,
							   PAGE_CACHE_SIZE - 1);
			}

			p->head_position = 0;
			rdev->raid_disk = mirror;
			err = 0;
			/* As all devices are equivalent, we don't need a full recovery
			 * if this was recently any drive of the array
			 */
			if (rdev->saved_raid_disk < 0)
				conf->fullsync = 1;
			rcu_assign_pointer(p->rdev, rdev);
			break;
		}
	md_integrity_add_rdev(rdev, mddev);
	print_conf(conf);
	return err;
}

static int raid1_remove_disk(mddev_t *mddev, int number)
{
	conf_t *conf = mddev->private;
	int err = 0;
	mdk_rdev_t *rdev;
	mirror_info_t *p = conf->mirrors+ number;

	print_conf(conf);
	rdev = p->rdev;
	if (rdev) {
		if (test_bit(In_sync, &rdev->flags) ||
		    atomic_read(&rdev->nr_pending)) {
			err = -EBUSY;
			goto abort;
		}
		/* Only remove non-faulty devices if recovery
		 * is not possible.
		 */
		if (!test_bit(Faulty, &rdev->flags) &&
		    mddev->degraded < conf->raid_disks) {
			err = -EBUSY;
			goto abort;
		}
		p->rdev = NULL;
		synchronize_rcu();
		if (atomic_read(&rdev->nr_pending)) {
			/* lost the race, try later */
			err = -EBUSY;
			p->rdev = rdev;
			goto abort;
		}
		md_integrity_register(mddev);
	}
abort:

	print_conf(conf);
	return err;
}


static void end_sync_read(struct bio *bio, int error)
{
	r1bio_t *r1_bio = bio->bi_private;
	int i;

	for (i=r1_bio->mddev->raid_disks; i--; )
		if (r1_bio->bios[i] == bio)
			break;
	BUG_ON(i < 0);
	update_head_pos(i, r1_bio);
	/*
	 * we have read a block, now it needs to be re-written,
	 * or re-read if the read failed.
	 * We don't do much here, just schedule handling by raid1d
	 */
	if (test_bit(BIO_UPTODATE, &bio->bi_flags))
		set_bit(R1BIO_Uptodate, &r1_bio->state);

	if (atomic_dec_and_test(&r1_bio->remaining))
		reschedule_retry(r1_bio);
}

static void end_sync_write(struct bio *bio, int error)
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	r1bio_t *r1_bio = bio->bi_private;
	mddev_t *mddev = r1_bio->mddev;
	conf_t *conf = mddev->private;
	int i;
	int mirror=0;

	for (i = 0; i < conf->raid_disks; i++)
		if (r1_bio->bios[i] == bio) {
			mirror = i;
			break;
		}
	if (!uptodate) {
		sector_t sync_blocks = 0;
		sector_t s = r1_bio->sector;
		long sectors_to_go = r1_bio->sectors;
		/* make sure these bits doesn't get cleared. */
		do {
			bitmap_end_sync(mddev->bitmap, s,
					&sync_blocks, 1);
			s += sync_blocks;
			sectors_to_go -= sync_blocks;
		} while (sectors_to_go > 0);
		md_error(mddev, conf->mirrors[mirror].rdev);
	}

	update_head_pos(mirror, r1_bio);

	if (atomic_dec_and_test(&r1_bio->remaining)) {
		sector_t s = r1_bio->sectors;
		put_buf(r1_bio);
		md_done_sync(mddev, s, uptodate);
	}
}

static void sync_request_write(mddev_t *mddev, r1bio_t *r1_bio)
{
	conf_t *conf = mddev->private;
	int i;
	int disks = conf->raid_disks;
	struct bio *bio, *wbio;

	bio = r1_bio->bios[r1_bio->read_disk];


	if (test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery)) {
		/* We have read all readable devices.  If we haven't
		 * got the block, then there is no hope left.
		 * If we have, then we want to do a comparison
		 * and skip the write if everything is the same.
		 * If any blocks failed to read, then we need to
		 * attempt an over-write
		 */
		int primary;
		if (!test_bit(R1BIO_Uptodate, &r1_bio->state)) {
			for (i=0; i<mddev->raid_disks; i++)
				if (r1_bio->bios[i]->bi_end_io == end_sync_read)
					md_error(mddev, conf->mirrors[i].rdev);

			md_done_sync(mddev, r1_bio->sectors, 1);
			put_buf(r1_bio);
			return;
		}
		for (primary=0; primary<mddev->raid_disks; primary++)
			if (r1_bio->bios[primary]->bi_end_io == end_sync_read &&
			    test_bit(BIO_UPTODATE, &r1_bio->bios[primary]->bi_flags)) {
				r1_bio->bios[primary]->bi_end_io = NULL;
				rdev_dec_pending(conf->mirrors[primary].rdev, mddev);
				break;
			}
		r1_bio->read_disk = primary;
		for (i=0; i<mddev->raid_disks; i++)
			if (r1_bio->bios[i]->bi_end_io == end_sync_read) {
				int j;
				int vcnt = r1_bio->sectors >> (PAGE_SHIFT- 9);
				struct bio *pbio = r1_bio->bios[primary];
				struct bio *sbio = r1_bio->bios[i];

				if (test_bit(BIO_UPTODATE, &sbio->bi_flags)) {
					for (j = vcnt; j-- ; ) {
						struct page *p, *s;
						p = pbio->bi_io_vec[j].bv_page;
						s = sbio->bi_io_vec[j].bv_page;
						if (memcmp(page_address(p),
							   page_address(s),
							   PAGE_SIZE))
							break;
					}
				} else
					j = 0;
				if (j >= 0)
					mddev->resync_mismatches += r1_bio->sectors;
				if (j < 0 || (test_bit(MD_RECOVERY_CHECK, &mddev->recovery)
					      && test_bit(BIO_UPTODATE, &sbio->bi_flags))) {
					sbio->bi_end_io = NULL;
					rdev_dec_pending(conf->mirrors[i].rdev, mddev);
				} else {
					/* fixup the bio for reuse */
					int size;
					sbio->bi_vcnt = vcnt;
					sbio->bi_size = r1_bio->sectors << 9;
					sbio->bi_idx = 0;
					sbio->bi_phys_segments = 0;
					sbio->bi_flags &= ~(BIO_POOL_MASK - 1);
					sbio->bi_flags |= 1 << BIO_UPTODATE;
					sbio->bi_next = NULL;
					sbio->bi_sector = r1_bio->sector +
						conf->mirrors[i].rdev->data_offset;
					sbio->bi_bdev = conf->mirrors[i].rdev->bdev;
					size = sbio->bi_size;
					for (j = 0; j < vcnt ; j++) {
						struct bio_vec *bi;
						bi = &sbio->bi_io_vec[j];
						bi->bv_offset = 0;
						if (size > PAGE_SIZE)
							bi->bv_len = PAGE_SIZE;
						else
							bi->bv_len = size;
						size -= PAGE_SIZE;
						memcpy(page_address(bi->bv_page),
						       page_address(pbio->bi_io_vec[j].bv_page),
						       PAGE_SIZE);
					}

				}
			}
	}
	if (!test_bit(R1BIO_Uptodate, &r1_bio->state)) {
		/* ouch - failed to read all of that.
		 * Try some synchronous reads of other devices to get
		 * good data, much like with normal read errors.  Only
		 * read into the pages we already have so we don't
		 * need to re-issue the read request.
		 * We don't need to freeze the array, because being in an
		 * active sync request, there is no normal IO, and
		 * no overlapping syncs.
		 */
		sector_t sect = r1_bio->sector;
		int sectors = r1_bio->sectors;
		int idx = 0;

		while(sectors) {
			int s = sectors;
			int d = r1_bio->read_disk;
			int success = 0;
			mdk_rdev_t *rdev;

			if (s > (PAGE_SIZE>>9))
				s = PAGE_SIZE >> 9;
			do {
				if (r1_bio->bios[d]->bi_end_io == end_sync_read) {
					/* No rcu protection needed here devices
					 * can only be removed when no resync is
					 * active, and resync is currently active
					 */
					rdev = conf->mirrors[d].rdev;
					if (sync_page_io(rdev,
							 sect + rdev->data_offset,
							 s<<9,
							 bio->bi_io_vec[idx].bv_page,
							 READ)) {
						success = 1;
						break;
					}
				}
				d++;
				if (d == conf->raid_disks)
					d = 0;
			} while (!success && d != r1_bio->read_disk);

			if (success) {
				int start = d;
				/* write it back and re-read */
				set_bit(R1BIO_Uptodate, &r1_bio->state);
				while (d != r1_bio->read_disk) {
					if (d == 0)
						d = conf->raid_disks;
					d--;
					if (r1_bio->bios[d]->bi_end_io != end_sync_read)
						continue;
					rdev = conf->mirrors[d].rdev;
					atomic_add(s, &rdev->corrected_errors);
					if (sync_page_io(rdev,
							 sect + rdev->data_offset,
							 s<<9,
							 bio->bi_io_vec[idx].bv_page,
							 WRITE) == 0)
						md_error(mddev, rdev);
				}
				d = start;
				while (d != r1_bio->read_disk) {
					if (d == 0)
						d = conf->raid_disks;
					d--;
					if (r1_bio->bios[d]->bi_end_io != end_sync_read)
						continue;
					rdev = conf->mirrors[d].rdev;
					if (sync_page_io(rdev,
							 sect + rdev->data_offset,
							 s<<9,
							 bio->bi_io_vec[idx].bv_page,
							 READ) == 0)
						md_error(mddev, rdev);
				}
			} else {
				char b[BDEVNAME_SIZE];
				/* Cannot read from anywhere, array is toast */
				md_error(mddev, conf->mirrors[r1_bio->read_disk].rdev);
				printk(KERN_ALERT "md/raid1:%s: %s: unrecoverable I/O read error"
				       " for block %llu\n",
				       mdname(mddev),
				       bdevname(bio->bi_bdev, b),
				       (unsigned long long)r1_bio->sector);
				md_done_sync(mddev, r1_bio->sectors, 0);
				put_buf(r1_bio);
				return;
			}
			sectors -= s;
			sect += s;
			idx ++;
		}
	}

	/*
	 * schedule writes
	 */
	atomic_set(&r1_bio->remaining, 1);
	for (i = 0; i < disks ; i++) {
		wbio = r1_bio->bios[i];
		if (wbio->bi_end_io == NULL ||
		    (wbio->bi_end_io == end_sync_read &&
		     (i == r1_bio->read_disk ||
		      !test_bit(MD_RECOVERY_SYNC, &mddev->recovery))))
			continue;

		wbio->bi_rw = WRITE;
		wbio->bi_end_io = end_sync_write;
		atomic_inc(&r1_bio->remaining);
		md_sync_acct(conf->mirrors[i].rdev->bdev, wbio->bi_size >> 9);

		generic_make_request(wbio);
	}

	if (atomic_dec_and_test(&r1_bio->remaining)) {
		/* if we're here, all write(s) have completed, so clean up */
		md_done_sync(mddev, r1_bio->sectors, 1);
		put_buf(r1_bio);
	}
}

/*
 * This is a kernel thread which:
 *
 *	1.	Retries failed read operations on working mirrors.
 *	2.	Updates the raid superblock when problems encounter.
 *	3.	Performs writes following reads for array syncronising.
 */

static void fix_read_error(conf_t *conf, int read_disk,
			   sector_t sect, int sectors)
{
	mddev_t *mddev = conf->mddev;
	while(sectors) {
		int s = sectors;
		int d = read_disk;
		int success = 0;
		int start;
		mdk_rdev_t *rdev;

		if (s > (PAGE_SIZE>>9))
			s = PAGE_SIZE >> 9;

		do {
			/* Note: no rcu protection needed here
			 * as this is synchronous in the raid1d thread
			 * which is the thread that might remove
			 * a device.  If raid1d ever becomes multi-threaded....
			 */
			rdev = conf->mirrors[d].rdev;
			if (rdev &&
			    test_bit(In_sync, &rdev->flags) &&
			    sync_page_io(rdev,
					 sect + rdev->data_offset,
					 s<<9,
					 conf->tmppage, READ))
				success = 1;
			else {
				d++;
				if (d == conf->raid_disks)
					d = 0;
			}
		} while (!success && d != read_disk);

		if (!success) {
			/* Cannot read from anywhere -- bye bye array */
			md_error(mddev, conf->mirrors[read_disk].rdev);
			break;
		}
		/* write it back and re-read */
		start = d;
		while (d != read_disk) {
			if (d==0)
				d = conf->raid_disks;
			d--;
			rdev = conf->mirrors[d].rdev;
			if (rdev &&
			    test_bit(In_sync, &rdev->flags)) {
				if (sync_page_io(rdev,
						 sect + rdev->data_offset,
						 s<<9, conf->tmppage, WRITE)
				    == 0)
					/* Well, this device is dead */
					md_error(mddev, rdev);
			}
		}
		d = start;
		while (d != read_disk) {
			char b[BDEVNAME_SIZE];
			if (d==0)
				d = conf->raid_disks;
			d--;
			rdev = conf->mirrors[d].rdev;
			if (rdev &&
			    test_bit(In_sync, &rdev->flags)) {
				if (sync_page_io(rdev,
						 sect + rdev->data_offset,
						 s<<9, conf->tmppage, READ)
				    == 0)
					/* Well, this device is dead */
					md_error(mddev, rdev);
				else {
					atomic_add(s, &rdev->corrected_errors);
					printk(KERN_INFO
					       "md/raid1:%s: read error corrected "
					       "(%d sectors at %llu on %s)\n",
					       mdname(mddev), s,
					       (unsigned long long)(sect +
					           rdev->data_offset),
					       bdevname(rdev->bdev, b));
				}
			}
		}
		sectors -= s;
		sect += s;
	}
}

static void raid1d(mddev_t *mddev)
{
	r1bio_t *r1_bio;
	struct bio *bio;
	unsigned long flags;
	conf_t *conf = mddev->private;
	struct list_head *head = &conf->retry_list;
	int unplug=0;
	mdk_rdev_t *rdev;

	md_check_recovery(mddev);
	
	for (;;) {
		char b[BDEVNAME_SIZE];

		unplug += flush_pending_writes(conf);

		spin_lock_irqsave(&conf->device_lock, flags);
		if (list_empty(head)) {
			spin_unlock_irqrestore(&conf->device_lock, flags);
			break;
		}
		r1_bio = list_entry(head->prev, r1bio_t, retry_list);
		list_del(head->prev);
		conf->nr_queued--;
		spin_unlock_irqrestore(&conf->device_lock, flags);

		mddev = r1_bio->mddev;
		conf = mddev->private;
		if (test_bit(R1BIO_IsSync, &r1_bio->state)) {
			sync_request_write(mddev, r1_bio);
			unplug = 1;
		} else {
			int disk;

			/* we got a read error. Maybe the drive is bad.  Maybe just
			 * the block and we can fix it.
			 * We freeze all other IO, and try reading the block from
			 * other devices.  When we find one, we re-write
			 * and check it that fixes the read error.
			 * This is all done synchronously while the array is
			 * frozen
			 */
			if (mddev->ro == 0) {
				freeze_array(conf);
				fix_read_error(conf, r1_bio->read_disk,
					       r1_bio->sector,
					       r1_bio->sectors);
				unfreeze_array(conf);
			} else
				md_error(mddev,
					 conf->mirrors[r1_bio->read_disk].rdev);

			bio = r1_bio->bios[r1_bio->read_disk];
			if ((disk=read_balance(conf, r1_bio)) == -1) {
				printk(KERN_ALERT "md/raid1:%s: %s: unrecoverable I/O"
				       " read error for block %llu\n",
				       mdname(mddev),
				       bdevname(bio->bi_bdev,b),
				       (unsigned long long)r1_bio->sector);
				raid_end_bio_io(r1_bio);
			} else {
				const unsigned long do_sync = r1_bio->master_bio->bi_rw & REQ_SYNC;
				r1_bio->bios[r1_bio->read_disk] =
					mddev->ro ? IO_BLOCKED : NULL;
				r1_bio->read_disk = disk;
				bio_put(bio);
				bio = bio_clone_mddev(r1_bio->master_bio,
						      GFP_NOIO, mddev);
				r1_bio->bios[r1_bio->read_disk] = bio;
				rdev = conf->mirrors[disk].rdev;
				if (printk_ratelimit())
					printk(KERN_ERR "md/raid1:%s: redirecting sector %llu to"
					       " other mirror: %s\n",
					       mdname(mddev),
					       (unsigned long long)r1_bio->sector,
					       bdevname(rdev->bdev,b));
				bio->bi_sector = r1_bio->sector + rdev->data_offset;
				bio->bi_bdev = rdev->bdev;
				bio->bi_end_io = raid1_end_read_request;
				bio->bi_rw = READ | do_sync;
				bio->bi_private = r1_bio;
				unplug = 1;
				generic_make_request(bio);
			}
		}
		cond_resched();
	}
	if (unplug)
		unplug_slaves(mddev);
}


static int init_resync(conf_t *conf)
{
	int buffs;

	buffs = RESYNC_WINDOW / RESYNC_BLOCK_SIZE;
	BUG_ON(conf->r1buf_pool);
	conf->r1buf_pool = mempool_create(buffs, r1buf_pool_alloc, r1buf_pool_free,
					  conf->poolinfo);
	if (!conf->r1buf_pool)
		return -ENOMEM;
	conf->next_resync = 0;
	return 0;
}

/*
 * perform a "sync" on one "block"
 *
 * We need to make sure that no normal I/O request - particularly write
 * requests - conflict with active sync requests.
 *
 * This is achieved by tracking pending requests and a 'barrier' concept
 * that can be installed to exclude normal IO requests.
 */

static sector_t sync_request(mddev_t *mddev, sector_t sector_nr, int *skipped, int go_faster)
{
	conf_t *conf = mddev->private;
	r1bio_t *r1_bio;
	struct bio *bio;
	sector_t max_sector, nr_sectors;
	int disk = -1;
	int i;
	int wonly = -1;
	int write_targets = 0, read_targets = 0;
	sector_t sync_blocks;
	int still_degraded = 0;

	if (!conf->r1buf_pool)
		if (init_resync(conf))
			return 0;

	max_sector = mddev->dev_sectors;
	if (sector_nr >= max_sector) {
		/* If we aborted, we need to abort the
		 * sync on the 'current' bitmap chunk (there will
		 * only be one in raid1 resync.
		 * We can find the current addess in mddev->curr_resync
		 */
		if (mddev->curr_resync < max_sector) /* aborted */
			bitmap_end_sync(mddev->bitmap, mddev->curr_resync,
						&sync_blocks, 1);
		else /* completed sync */
			conf->fullsync = 0;

		bitmap_close_sync(mddev->bitmap);
		close_sync(conf);
		return 0;
	}

	if (mddev->bitmap == NULL &&
	    mddev->recovery_cp == MaxSector &&
	    !test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery) &&
	    conf->fullsync == 0) {
		*skipped = 1;
		return max_sector - sector_nr;
	}
	/* before building a request, check if we can skip these blocks..
	 * This call the bitmap_start_sync doesn't actually record anything
	 */
	if (!bitmap_start_sync(mddev->bitmap, sector_nr, &sync_blocks, 1) &&
	    !conf->fullsync && !test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery)) {
		/* We can skip this block, and probably several more */
		*skipped = 1;
		return sync_blocks;
	}
	/*
	 * If there is non-resync activity waiting for a turn,
	 * and resync is going fast enough,
	 * then let it though before starting on this new sync request.
	 */
	if (!go_faster && conf->nr_waiting)
		msleep_interruptible(1000);

	bitmap_cond_end_sync(mddev->bitmap, sector_nr);
	r1_bio = mempool_alloc(conf->r1buf_pool, GFP_NOIO);
	raise_barrier(conf);

	conf->next_resync = sector_nr;

	rcu_read_lock();
	/*
	 * If we get a correctably read error during resync or recovery,
	 * we might want to read from a different device.  So we
	 * flag all drives that could conceivably be read from for READ,
	 * and any others (which will be non-In_sync devices) for WRITE.
	 * If a read fails, we try reading from something else for which READ
	 * is OK.
	 */

	r1_bio->mddev = mddev;
	r1_bio->sector = sector_nr;
	r1_bio->state = 0;
	set_bit(R1BIO_IsSync, &r1_bio->state);

	for (i=0; i < conf->raid_disks; i++) {
		mdk_rdev_t *rdev;
		bio = r1_bio->bios[i];

		/* take from bio_init */
		bio->bi_next = NULL;
		bio->bi_flags &= ~(BIO_POOL_MASK-1);
		bio->bi_flags |= 1 << BIO_UPTODATE;
		bio->bi_comp_cpu = -1;
		bio->bi_rw = READ;
		bio->bi_vcnt = 0;
		bio->bi_idx = 0;
		bio->bi_phys_segments = 0;
		bio->bi_size = 0;
		bio->bi_end_io = NULL;
		bio->bi_private = NULL;

		rdev = rcu_dereference(conf->mirrors[i].rdev);
		if (rdev == NULL ||
			   test_bit(Faulty, &rdev->flags)) {
			still_degraded = 1;
			continue;
		} else if (!test_bit(In_sync, &rdev->flags)) {
			bio->bi_rw = WRITE;
			bio->bi_end_io = end_sync_write;
			write_targets ++;
		} else {
			/* may need to read from here */
			bio->bi_rw = READ;
			bio->bi_end_io = end_sync_read;
			if (test_bit(WriteMostly, &rdev->flags)) {
				if (wonly < 0)
					wonly = i;
			} else {
				if (disk < 0)
					disk = i;
			}
			read_targets++;
		}
		atomic_inc(&rdev->nr_pending);
		bio->bi_sector = sector_nr + rdev->data_offset;
		bio->bi_bdev = rdev->bdev;
		bio->bi_private = r1_bio;
	}
	rcu_read_unlock();
	if (disk < 0)
		disk = wonly;
	r1_bio->read_disk = disk;

	if (test_bit(MD_RECOVERY_SYNC, &mddev->recovery) && read_targets > 0)
		/* extra read targets are also write targets */
		write_targets += read_targets-1;

	if (write_targets == 0 || read_targets == 0) {
		/* There is nowhere to write, so all non-sync
		 * drives must be failed - so we are finished
		 */
		sector_t rv = max_sector - sector_nr;
		*skipped = 1;
		put_buf(r1_bio);
		return rv;
	}

	if (max_sector > mddev->resync_max)
		max_sector = mddev->resync_max; /* Don't do IO beyond here */
	nr_sectors = 0;
	sync_blocks = 0;
	do {
		struct page *page;
		int len = PAGE_SIZE;
		if (sector_nr + (len>>9) > max_sector)
			len = (max_sector - sector_nr) << 9;
		if (len == 0)
			break;
		if (sync_blocks == 0) {
			if (!bitmap_start_sync(mddev->bitmap, sector_nr,
					       &sync_blocks, still_degraded) &&
			    !conf->fullsync &&
			    !test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery))
				break;
			BUG_ON(sync_blocks < (PAGE_SIZE>>9));
			if ((len >> 9) > sync_blocks)
				len = sync_blocks<<9;
		}

		for (i=0 ; i < conf->raid_disks; i++) {
			bio = r1_bio->bios[i];
			if (bio->bi_end_io) {
				page = bio->bi_io_vec[bio->bi_vcnt].bv_page;
				if (bio_add_page(bio, page, len, 0) == 0) {
					/* stop here */
					bio->bi_io_vec[bio->bi_vcnt].bv_page = page;
					while (i > 0) {
						i--;
						bio = r1_bio->bios[i];
						if (bio->bi_end_io==NULL)
							continue;
						/* remove last page from this bio */
						bio->bi_vcnt--;
						bio->bi_size -= len;
						bio->bi_flags &= ~(1<< BIO_SEG_VALID);
					}
					goto bio_full;
				}
			}
		}
		nr_sectors += len>>9;
		sector_nr += len>>9;
		sync_blocks -= (len>>9);
	} while (r1_bio->bios[disk]->bi_vcnt < RESYNC_PAGES);
 bio_full:
	r1_bio->sectors = nr_sectors;

	/* For a user-requested sync, we read all readable devices and do a
	 * compare
	 */
	if (test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery)) {
		atomic_set(&r1_bio->remaining, read_targets);
		for (i=0; i<conf->raid_disks; i++) {
			bio = r1_bio->bios[i];
			if (bio->bi_end_io == end_sync_read) {
				md_sync_acct(bio->bi_bdev, nr_sectors);
				generic_make_request(bio);
			}
		}
	} else {
		atomic_set(&r1_bio->remaining, 1);
		bio = r1_bio->bios[r1_bio->read_disk];
		md_sync_acct(bio->bi_bdev, nr_sectors);
		generic_make_request(bio);

	}
	return nr_sectors;
}

static sector_t raid1_size(mddev_t *mddev, sector_t sectors, int raid_disks)
{
	if (sectors)
		return sectors;

	return mddev->dev_sectors;
}

static conf_t *setup_conf(mddev_t *mddev)
{
	conf_t *conf;
	int i;
	mirror_info_t *disk;
	mdk_rdev_t *rdev;
	int err = -ENOMEM;

	conf = kzalloc(sizeof(conf_t), GFP_KERNEL);
	if (!conf)
		goto abort;

	conf->mirrors = kzalloc(sizeof(struct mirror_info)*mddev->raid_disks,
				 GFP_KERNEL);
	if (!conf->mirrors)
		goto abort;

	conf->tmppage = alloc_page(GFP_KERNEL);
	if (!conf->tmppage)
		goto abort;

	conf->poolinfo = kzalloc(sizeof(*conf->poolinfo), GFP_KERNEL);
	if (!conf->poolinfo)
		goto abort;
	conf->poolinfo->raid_disks = mddev->raid_disks;
	conf->r1bio_pool = mempool_create(NR_RAID1_BIOS, r1bio_pool_alloc,
					  r1bio_pool_free,
					  conf->poolinfo);
	if (!conf->r1bio_pool)
		goto abort;

	conf->poolinfo->mddev = mddev;

	spin_lock_init(&conf->device_lock);
	list_for_each_entry(rdev, &mddev->disks, same_set) {
		int disk_idx = rdev->raid_disk;
		if (disk_idx >= mddev->raid_disks
		    || disk_idx < 0)
			continue;
		disk = conf->mirrors + disk_idx;

		disk->rdev = rdev;

		disk->head_position = 0;
	}
	conf->raid_disks = mddev->raid_disks;
	conf->mddev = mddev;
	INIT_LIST_HEAD(&conf->retry_list);

	spin_lock_init(&conf->resync_lock);
	init_waitqueue_head(&conf->wait_barrier);

	bio_list_init(&conf->pending_bio_list);

	conf->last_used = -1;
	for (i = 0; i < conf->raid_disks; i++) {

		disk = conf->mirrors + i;

		if (!disk->rdev ||
		    !test_bit(In_sync, &disk->rdev->flags)) {
			disk->head_position = 0;
			if (disk->rdev)
				conf->fullsync = 1;
		} else if (conf->last_used < 0)
			/*
			 * The first working device is used as a
			 * starting point to read balancing.
			 */
			conf->last_used = i;
	}

	err = -EIO;
	if (conf->last_used < 0) {
		printk(KERN_ERR "md/raid1:%s: no operational mirrors\n",
		       mdname(mddev));
		goto abort;
	}
	err = -ENOMEM;
	conf->thread = md_register_thread(raid1d, mddev, NULL);
	if (!conf->thread) {
		printk(KERN_ERR
		       "md/raid1:%s: couldn't allocate thread\n",
		       mdname(mddev));
		goto abort;
	}

	return conf;

 abort:
	if (conf) {
		if (conf->r1bio_pool)
			mempool_destroy(conf->r1bio_pool);
		kfree(conf->mirrors);
		safe_put_page(conf->tmppage);
		kfree(conf->poolinfo);
		kfree(conf);
	}
	return ERR_PTR(err);
}

static int run(mddev_t *mddev)
{
	conf_t *conf;
	int i;
	mdk_rdev_t *rdev;

	if (mddev->level != 1) {
		printk(KERN_ERR "md/raid1:%s: raid level not set to mirroring (%d)\n",
		       mdname(mddev), mddev->level);
		return -EIO;
	}
	if (mddev->reshape_position != MaxSector) {
		printk(KERN_ERR "md/raid1:%s: reshape_position set but not supported\n",
		       mdname(mddev));
		return -EIO;
	}
	/*
	 * copy the already verified devices into our private RAID1
	 * bookkeeping area. [whatever we allocate in run(),
	 * should be freed in stop()]
	 */
	if (mddev->private == NULL)
		conf = setup_conf(mddev);
	else
		conf = mddev->private;

	if (IS_ERR(conf))
		return PTR_ERR(conf);

	mddev->queue->queue_lock = &conf->device_lock;
	list_for_each_entry(rdev, &mddev->disks, same_set) {
		disk_stack_limits(mddev->gendisk, rdev->bdev,
				  rdev->data_offset << 9);
		/* as we don't honour merge_bvec_fn, we must never risk
		 * violating it, so limit ->max_segments to 1 lying within
		 * a single page, as a one page request is never in violation.
		 */
		if (rdev->bdev->bd_disk->queue->merge_bvec_fn) {
			blk_queue_max_segments(mddev->queue, 1);
			blk_queue_segment_boundary(mddev->queue,
						   PAGE_CACHE_SIZE - 1);
		}
	}

	mddev->degraded = 0;
	for (i=0; i < conf->raid_disks; i++)
		if (conf->mirrors[i].rdev == NULL ||
		    !test_bit(In_sync, &conf->mirrors[i].rdev->flags) ||
		    test_bit(Faulty, &conf->mirrors[i].rdev->flags))
			mddev->degraded++;

	if (conf->raid_disks - mddev->degraded == 1)
		mddev->recovery_cp = MaxSector;

	if (mddev->recovery_cp != MaxSector)
		printk(KERN_NOTICE "md/raid1:%s: not clean"
		       " -- starting background reconstruction\n",
		       mdname(mddev));
	printk(KERN_INFO 
		"md/raid1:%s: active with %d out of %d mirrors\n",
		mdname(mddev), mddev->raid_disks - mddev->degraded, 
		mddev->raid_disks);

	/*
	 * Ok, everything is just fine now
	 */
	mddev->thread = conf->thread;
	conf->thread = NULL;
	mddev->private = conf;

	md_set_array_sectors(mddev, raid1_size(mddev, 0, 0));

	mddev->queue->unplug_fn = raid1_unplug;
	mddev->queue->backing_dev_info.congested_fn = raid1_congested;
	mddev->queue->backing_dev_info.congested_data = mddev;
	md_integrity_register(mddev);
	return 0;
}

static int stop(mddev_t *mddev)
{
	conf_t *conf = mddev->private;
	struct bitmap *bitmap = mddev->bitmap;

	/* wait for behind writes to complete */
	if (bitmap && atomic_read(&bitmap->behind_writes) > 0) {
		printk(KERN_INFO "md/raid1:%s: behind writes in progress - waiting to stop.\n",
		       mdname(mddev));
		/* need to kick something here to make sure I/O goes? */
		wait_event(bitmap->behind_wait,
			   atomic_read(&bitmap->behind_writes) == 0);
	}

	raise_barrier(conf);
	lower_barrier(conf);

	md_unregister_thread(mddev->thread);
	mddev->thread = NULL;
	blk_sync_queue(mddev->queue); /* the unplug fn references 'conf'*/
	if (conf->r1bio_pool)
		mempool_destroy(conf->r1bio_pool);
	kfree(conf->mirrors);
	kfree(conf->poolinfo);
	kfree(conf);
	mddev->private = NULL;
	return 0;
}

static int raid1_resize(mddev_t *mddev, sector_t sectors)
{
	/* no resync is happening, and there is enough space
	 * on all devices, so we can resize.
	 * We need to make sure resync covers any new space.
	 * If the array is shrinking we should possibly wait until
	 * any io in the removed space completes, but it hardly seems
	 * worth it.
	 */
	md_set_array_sectors(mddev, raid1_size(mddev, sectors, 0));
	if (mddev->array_sectors > raid1_size(mddev, sectors, 0))
		return -EINVAL;
	set_capacity(mddev->gendisk, mddev->array_sectors);
	revalidate_disk(mddev->gendisk);
	if (sectors > mddev->dev_sectors &&
	    mddev->recovery_cp == MaxSector) {
		mddev->recovery_cp = mddev->dev_sectors;
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	}
	mddev->dev_sectors = sectors;
	mddev->resync_max_sectors = sectors;
	return 0;
}

static int raid1_reshape(mddev_t *mddev)
{
	/* We need to:
	 * 1/ resize the r1bio_pool
	 * 2/ resize conf->mirrors
	 *
	 * We allocate a new r1bio_pool if we can.
	 * Then raise a device barrier and wait until all IO stops.
	 * Then resize conf->mirrors and swap in the new r1bio pool.
	 *
	 * At the same time, we "pack" the devices so that all the missing
	 * devices have the higher raid_disk numbers.
	 */
	mempool_t *newpool, *oldpool;
	struct pool_info *newpoolinfo;
	mirror_info_t *newmirrors;
	conf_t *conf = mddev->private;
	int cnt, raid_disks;
	unsigned long flags;
	int d, d2, err;

	/* Cannot change chunk_size, layout, or level */
	if (mddev->chunk_sectors != mddev->new_chunk_sectors ||
	    mddev->layout != mddev->new_layout ||
	    mddev->level != mddev->new_level) {
		mddev->new_chunk_sectors = mddev->chunk_sectors;
		mddev->new_layout = mddev->layout;
		mddev->new_level = mddev->level;
		return -EINVAL;
	}

	err = md_allow_write(mddev);
	if (err)
		return err;

	raid_disks = mddev->raid_disks + mddev->delta_disks;

	if (raid_disks < conf->raid_disks) {
		cnt=0;
		for (d= 0; d < conf->raid_disks; d++)
			if (conf->mirrors[d].rdev)
				cnt++;
		if (cnt > raid_disks)
			return -EBUSY;
	}

	newpoolinfo = kmalloc(sizeof(*newpoolinfo), GFP_KERNEL);
	if (!newpoolinfo)
		return -ENOMEM;
	newpoolinfo->mddev = mddev;
	newpoolinfo->raid_disks = raid_disks;

	newpool = mempool_create(NR_RAID1_BIOS, r1bio_pool_alloc,
				 r1bio_pool_free, newpoolinfo);
	if (!newpool) {
		kfree(newpoolinfo);
		return -ENOMEM;
	}
	newmirrors = kzalloc(sizeof(struct mirror_info) * raid_disks, GFP_KERNEL);
	if (!newmirrors) {
		kfree(newpoolinfo);
		mempool_destroy(newpool);
		return -ENOMEM;
	}

	raise_barrier(conf);

	/* ok, everything is stopped */
	oldpool = conf->r1bio_pool;
	conf->r1bio_pool = newpool;

	for (d = d2 = 0; d < conf->raid_disks; d++) {
		mdk_rdev_t *rdev = conf->mirrors[d].rdev;
		if (rdev && rdev->raid_disk != d2) {
			char nm[20];
			sprintf(nm, "rd%d", rdev->raid_disk);
			sysfs_remove_link(&mddev->kobj, nm);
			rdev->raid_disk = d2;
			sprintf(nm, "rd%d", rdev->raid_disk);
			sysfs_remove_link(&mddev->kobj, nm);
			if (sysfs_create_link(&mddev->kobj,
					      &rdev->kobj, nm))
				printk(KERN_WARNING
				       "md/raid1:%s: cannot register "
				       "%s\n",
				       mdname(mddev), nm);
		}
		if (rdev)
			newmirrors[d2++].rdev = rdev;
	}
	kfree(conf->mirrors);
	conf->mirrors = newmirrors;
	kfree(conf->poolinfo);
	conf->poolinfo = newpoolinfo;

	spin_lock_irqsave(&conf->device_lock, flags);
	mddev->degraded += (raid_disks - conf->raid_disks);
	spin_unlock_irqrestore(&conf->device_lock, flags);
	conf->raid_disks = mddev->raid_disks = raid_disks;
	mddev->delta_disks = 0;

	conf->last_used = 0; /* just make sure it is in-range */
	lower_barrier(conf);

	set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	md_wakeup_thread(mddev->thread);

	mempool_destroy(oldpool);
	return 0;
}

static void raid1_quiesce(mddev_t *mddev, int state)
{
	conf_t *conf = mddev->private;

	switch(state) {
	case 2: /* wake for suspend */
		wake_up(&conf->wait_barrier);
		break;
	case 1:
		raise_barrier(conf);
		break;
	case 0:
		lower_barrier(conf);
		break;
	}
}

static void *raid1_takeover(mddev_t *mddev)
{
	/* raid1 can take over:
	 *  raid5 with 2 devices, any layout or chunk size
	 */
	if (mddev->level == 5 && mddev->raid_disks == 2) {
		conf_t *conf;
		mddev->new_level = 1;
		mddev->new_layout = 0;
		mddev->new_chunk_sectors = 0;
		conf = setup_conf(mddev);
		if (!IS_ERR(conf))
			conf->barrier = 1;
		return conf;
	}
	return ERR_PTR(-EINVAL);
}

static struct mdk_personality raid1_personality =
{
	.name		= "raid1",
	.level		= 1,
	.owner		= THIS_MODULE,
	.make_request	= make_request,
	.run		= run,
	.stop		= stop,
	.status		= status,
	.error_handler	= error,
	.hot_add_disk	= raid1_add_disk,
	.hot_remove_disk= raid1_remove_disk,
	.spare_active	= raid1_spare_active,
	.sync_request	= sync_request,
	.resize		= raid1_resize,
	.size		= raid1_size,
	.check_reshape	= raid1_reshape,
	.quiesce	= raid1_quiesce,
	.takeover	= raid1_takeover,
};

static int __init raid_init(void)
{
	return register_md_personality(&raid1_personality);
}

static void raid_exit(void)
{
	unregister_md_personality(&raid1_personality);
}

module_init(raid_init);
module_exit(raid_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RAID1 (mirroring) personality for MD");
MODULE_ALIAS("md-personality-3"); /* RAID1 */
MODULE_ALIAS("md-raid1");
MODULE_ALIAS("md-level-1");
