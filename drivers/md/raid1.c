// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/ratelimit.h>
#include <linux/interval_tree_generic.h>

#include <trace/events/block.h>

#include "md.h"
#include "raid1.h"
#include "md-bitmap.h"

#define UNSUPPORTED_MDDEV_FLAGS		\
	((1L << MD_HAS_JOURNAL) |	\
	 (1L << MD_JOURNAL_CLEAN) |	\
	 (1L << MD_HAS_PPL) |		\
	 (1L << MD_HAS_MULTIPLE_PPLS))

static void allow_barrier(struct r1conf *conf, sector_t sector_nr);
static void lower_barrier(struct r1conf *conf, sector_t sector_nr);

#define RAID_1_10_NAME "raid1"
#include "raid1-10.c"

#define START(node) ((node)->start)
#define LAST(node) ((node)->last)
INTERVAL_TREE_DEFINE(struct serial_info, node, sector_t, _subtree_last,
		     START, LAST, static inline, raid1_rb);

static int check_and_add_serial(struct md_rdev *rdev, struct r1bio *r1_bio,
				struct serial_info *si, int idx)
{
	unsigned long flags;
	int ret = 0;
	sector_t lo = r1_bio->sector;
	sector_t hi = lo + r1_bio->sectors;
	struct serial_in_rdev *serial = &rdev->serial[idx];

	spin_lock_irqsave(&serial->serial_lock, flags);
	/* collision happened */
	if (raid1_rb_iter_first(&serial->serial_rb, lo, hi))
		ret = -EBUSY;
	else {
		si->start = lo;
		si->last = hi;
		raid1_rb_insert(si, &serial->serial_rb);
	}
	spin_unlock_irqrestore(&serial->serial_lock, flags);

	return ret;
}

static void wait_for_serialization(struct md_rdev *rdev, struct r1bio *r1_bio)
{
	struct mddev *mddev = rdev->mddev;
	struct serial_info *si;
	int idx = sector_to_idx(r1_bio->sector);
	struct serial_in_rdev *serial = &rdev->serial[idx];

	if (WARN_ON(!mddev->serial_info_pool))
		return;
	si = mempool_alloc(mddev->serial_info_pool, GFP_NOIO);
	wait_event(serial->serial_io_wait,
		   check_and_add_serial(rdev, r1_bio, si, idx) == 0);
}

static void remove_serial(struct md_rdev *rdev, sector_t lo, sector_t hi)
{
	struct serial_info *si;
	unsigned long flags;
	int found = 0;
	struct mddev *mddev = rdev->mddev;
	int idx = sector_to_idx(lo);
	struct serial_in_rdev *serial = &rdev->serial[idx];

	spin_lock_irqsave(&serial->serial_lock, flags);
	for (si = raid1_rb_iter_first(&serial->serial_rb, lo, hi);
	     si; si = raid1_rb_iter_next(si, lo, hi)) {
		if (si->start == lo && si->last == hi) {
			raid1_rb_remove(si, &serial->serial_rb);
			mempool_free(si, mddev->serial_info_pool);
			found = 1;
			break;
		}
	}
	if (!found)
		WARN(1, "The write IO is not recorded for serialization\n");
	spin_unlock_irqrestore(&serial->serial_lock, flags);
	wake_up(&serial->serial_io_wait);
}

/*
 * for resync bio, r1bio pointer can be retrieved from the per-bio
 * 'struct resync_pages'.
 */
static inline struct r1bio *get_resync_r1bio(struct bio *bio)
{
	return get_resync_pages(bio)->raid_bio;
}

static void * r1bio_pool_alloc(gfp_t gfp_flags, void *data)
{
	struct pool_info *pi = data;
	int size = offsetof(struct r1bio, bios[pi->raid_disks]);

	/* allocate a r1bio with room for raid_disks entries in the bios array */
	return kzalloc(size, gfp_flags);
}

#define RESYNC_DEPTH 32
#define RESYNC_SECTORS (RESYNC_BLOCK_SIZE >> 9)
#define RESYNC_WINDOW (RESYNC_BLOCK_SIZE * RESYNC_DEPTH)
#define RESYNC_WINDOW_SECTORS (RESYNC_WINDOW >> 9)
#define CLUSTER_RESYNC_WINDOW (16 * RESYNC_WINDOW)
#define CLUSTER_RESYNC_WINDOW_SECTORS (CLUSTER_RESYNC_WINDOW >> 9)

static void * r1buf_pool_alloc(gfp_t gfp_flags, void *data)
{
	struct pool_info *pi = data;
	struct r1bio *r1_bio;
	struct bio *bio;
	int need_pages;
	int j;
	struct resync_pages *rps;

	r1_bio = r1bio_pool_alloc(gfp_flags, pi);
	if (!r1_bio)
		return NULL;

	rps = kmalloc_array(pi->raid_disks, sizeof(struct resync_pages),
			    gfp_flags);
	if (!rps)
		goto out_free_r1bio;

	/*
	 * Allocate bios : 1 for reading, n-1 for writing
	 */
	for (j = pi->raid_disks ; j-- ; ) {
		bio = bio_kmalloc(RESYNC_PAGES, gfp_flags);
		if (!bio)
			goto out_free_bio;
		bio_init(bio, NULL, bio->bi_inline_vecs, RESYNC_PAGES, 0);
		r1_bio->bios[j] = bio;
	}
	/*
	 * Allocate RESYNC_PAGES data pages and attach them to
	 * the first bio.
	 * If this is a user-requested check/repair, allocate
	 * RESYNC_PAGES for each bio.
	 */
	if (test_bit(MD_RECOVERY_REQUESTED, &pi->mddev->recovery))
		need_pages = pi->raid_disks;
	else
		need_pages = 1;
	for (j = 0; j < pi->raid_disks; j++) {
		struct resync_pages *rp = &rps[j];

		bio = r1_bio->bios[j];

		if (j < need_pages) {
			if (resync_alloc_pages(rp, gfp_flags))
				goto out_free_pages;
		} else {
			memcpy(rp, &rps[0], sizeof(*rp));
			resync_get_all_pages(rp);
		}

		rp->raid_bio = r1_bio;
		bio->bi_private = rp;
	}

	r1_bio->master_bio = NULL;

	return r1_bio;

out_free_pages:
	while (--j >= 0)
		resync_free_pages(&rps[j]);

out_free_bio:
	while (++j < pi->raid_disks) {
		bio_uninit(r1_bio->bios[j]);
		kfree(r1_bio->bios[j]);
	}
	kfree(rps);

out_free_r1bio:
	rbio_pool_free(r1_bio, data);
	return NULL;
}

static void r1buf_pool_free(void *__r1_bio, void *data)
{
	struct pool_info *pi = data;
	int i;
	struct r1bio *r1bio = __r1_bio;
	struct resync_pages *rp = NULL;

	for (i = pi->raid_disks; i--; ) {
		rp = get_resync_pages(r1bio->bios[i]);
		resync_free_pages(rp);
		bio_uninit(r1bio->bios[i]);
		kfree(r1bio->bios[i]);
	}

	/* resync pages array stored in the 1st bio's .bi_private */
	kfree(rp);

	rbio_pool_free(r1bio, data);
}

static void put_all_bios(struct r1conf *conf, struct r1bio *r1_bio)
{
	int i;

	for (i = 0; i < conf->raid_disks * 2; i++) {
		struct bio **bio = r1_bio->bios + i;
		if (!BIO_SPECIAL(*bio))
			bio_put(*bio);
		*bio = NULL;
	}
}

static void free_r1bio(struct r1bio *r1_bio)
{
	struct r1conf *conf = r1_bio->mddev->private;

	put_all_bios(conf, r1_bio);
	mempool_free(r1_bio, &conf->r1bio_pool);
}

static void put_buf(struct r1bio *r1_bio)
{
	struct r1conf *conf = r1_bio->mddev->private;
	sector_t sect = r1_bio->sector;
	int i;

	for (i = 0; i < conf->raid_disks * 2; i++) {
		struct bio *bio = r1_bio->bios[i];
		if (bio->bi_end_io)
			rdev_dec_pending(conf->mirrors[i].rdev, r1_bio->mddev);
	}

	mempool_free(r1_bio, &conf->r1buf_pool);

	lower_barrier(conf, sect);
}

static void reschedule_retry(struct r1bio *r1_bio)
{
	unsigned long flags;
	struct mddev *mddev = r1_bio->mddev;
	struct r1conf *conf = mddev->private;
	int idx;

	idx = sector_to_idx(r1_bio->sector);
	spin_lock_irqsave(&conf->device_lock, flags);
	list_add(&r1_bio->retry_list, &conf->retry_list);
	atomic_inc(&conf->nr_queued[idx]);
	spin_unlock_irqrestore(&conf->device_lock, flags);

	wake_up(&conf->wait_barrier);
	md_wakeup_thread(mddev->thread);
}

/*
 * raid_end_bio_io() is called when we have finished servicing a mirrored
 * operation and are ready to return a success/failure code to the buffer
 * cache layer.
 */
static void call_bio_endio(struct r1bio *r1_bio)
{
	struct bio *bio = r1_bio->master_bio;

	if (!test_bit(R1BIO_Uptodate, &r1_bio->state))
		bio->bi_status = BLK_STS_IOERR;

	bio_endio(bio);
}

static void raid_end_bio_io(struct r1bio *r1_bio)
{
	struct bio *bio = r1_bio->master_bio;
	struct r1conf *conf = r1_bio->mddev->private;
	sector_t sector = r1_bio->sector;

	/* if nobody has done the final endio yet, do it now */
	if (!test_and_set_bit(R1BIO_Returned, &r1_bio->state)) {
		pr_debug("raid1: sync end %s on sectors %llu-%llu\n",
			 (bio_data_dir(bio) == WRITE) ? "write" : "read",
			 (unsigned long long) bio->bi_iter.bi_sector,
			 (unsigned long long) bio_end_sector(bio) - 1);

		call_bio_endio(r1_bio);
	}

	free_r1bio(r1_bio);
	/*
	 * Wake up any possible resync thread that waits for the device
	 * to go idle.  All I/Os, even write-behind writes, are done.
	 */
	allow_barrier(conf, sector);
}

/*
 * Update disk head position estimator based on IRQ completion info.
 */
static inline void update_head_pos(int disk, struct r1bio *r1_bio)
{
	struct r1conf *conf = r1_bio->mddev->private;

	conf->mirrors[disk].head_position =
		r1_bio->sector + (r1_bio->sectors);
}

/*
 * Find the disk number which triggered given bio
 */
static int find_bio_disk(struct r1bio *r1_bio, struct bio *bio)
{
	int mirror;
	struct r1conf *conf = r1_bio->mddev->private;
	int raid_disks = conf->raid_disks;

	for (mirror = 0; mirror < raid_disks * 2; mirror++)
		if (r1_bio->bios[mirror] == bio)
			break;

	BUG_ON(mirror == raid_disks * 2);
	update_head_pos(mirror, r1_bio);

	return mirror;
}

static void raid1_end_read_request(struct bio *bio)
{
	int uptodate = !bio->bi_status;
	struct r1bio *r1_bio = bio->bi_private;
	struct r1conf *conf = r1_bio->mddev->private;
	struct md_rdev *rdev = conf->mirrors[r1_bio->read_disk].rdev;

	/*
	 * this branch is our 'one mirror IO has finished' event handler:
	 */
	update_head_pos(r1_bio->read_disk, r1_bio);

	if (uptodate)
		set_bit(R1BIO_Uptodate, &r1_bio->state);
	else if (test_bit(FailFast, &rdev->flags) &&
		 test_bit(R1BIO_FailFast, &r1_bio->state))
		/* This was a fail-fast read so we definitely
		 * want to retry */
		;
	else {
		/* If all other devices have failed, we want to return
		 * the error upwards rather than fail the last device.
		 * Here we redefine "uptodate" to mean "Don't want to retry"
		 */
		unsigned long flags;
		spin_lock_irqsave(&conf->device_lock, flags);
		if (r1_bio->mddev->degraded == conf->raid_disks ||
		    (r1_bio->mddev->degraded == conf->raid_disks-1 &&
		     test_bit(In_sync, &rdev->flags)))
			uptodate = 1;
		spin_unlock_irqrestore(&conf->device_lock, flags);
	}

	if (uptodate) {
		raid_end_bio_io(r1_bio);
		rdev_dec_pending(rdev, conf->mddev);
	} else {
		/*
		 * oops, read error:
		 */
		pr_err_ratelimited("md/raid1:%s: %pg: rescheduling sector %llu\n",
				   mdname(conf->mddev),
				   rdev->bdev,
				   (unsigned long long)r1_bio->sector);
		set_bit(R1BIO_ReadError, &r1_bio->state);
		reschedule_retry(r1_bio);
		/* don't drop the reference on read_disk yet */
	}
}

static void close_write(struct r1bio *r1_bio)
{
	struct mddev *mddev = r1_bio->mddev;

	/* it really is the end of this request */
	if (test_bit(R1BIO_BehindIO, &r1_bio->state)) {
		bio_free_pages(r1_bio->behind_master_bio);
		bio_put(r1_bio->behind_master_bio);
		r1_bio->behind_master_bio = NULL;
	}

	if (test_bit(R1BIO_BehindIO, &r1_bio->state))
		mddev->bitmap_ops->end_behind_write(mddev);
	md_write_end(mddev);
}

static void r1_bio_write_done(struct r1bio *r1_bio)
{
	if (!atomic_dec_and_test(&r1_bio->remaining))
		return;

	if (test_bit(R1BIO_WriteError, &r1_bio->state))
		reschedule_retry(r1_bio);
	else {
		close_write(r1_bio);
		if (test_bit(R1BIO_MadeGood, &r1_bio->state))
			reschedule_retry(r1_bio);
		else
			raid_end_bio_io(r1_bio);
	}
}

static void raid1_end_write_request(struct bio *bio)
{
	struct r1bio *r1_bio = bio->bi_private;
	int behind = test_bit(R1BIO_BehindIO, &r1_bio->state);
	struct r1conf *conf = r1_bio->mddev->private;
	struct bio *to_put = NULL;
	int mirror = find_bio_disk(r1_bio, bio);
	struct md_rdev *rdev = conf->mirrors[mirror].rdev;
	bool discard_error;
	sector_t lo = r1_bio->sector;
	sector_t hi = r1_bio->sector + r1_bio->sectors;

	discard_error = bio->bi_status && bio_op(bio) == REQ_OP_DISCARD;

	/*
	 * 'one mirror IO has finished' event handler:
	 */
	if (bio->bi_status && !discard_error) {
		set_bit(WriteErrorSeen,	&rdev->flags);
		if (!test_and_set_bit(WantReplacement, &rdev->flags))
			set_bit(MD_RECOVERY_NEEDED, &
				conf->mddev->recovery);

		if (test_bit(FailFast, &rdev->flags) &&
		    (bio->bi_opf & MD_FAILFAST) &&
		    /* We never try FailFast to WriteMostly devices */
		    !test_bit(WriteMostly, &rdev->flags)) {
			md_error(r1_bio->mddev, rdev);
		}

		/*
		 * When the device is faulty, it is not necessary to
		 * handle write error.
		 */
		if (!test_bit(Faulty, &rdev->flags))
			set_bit(R1BIO_WriteError, &r1_bio->state);
		else {
			/* Finished with this branch */
			r1_bio->bios[mirror] = NULL;
			to_put = bio;
		}
	} else {
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
		r1_bio->bios[mirror] = NULL;
		to_put = bio;
		/*
		 * Do not set R1BIO_Uptodate if the current device is
		 * rebuilding or Faulty. This is because we cannot use
		 * such device for properly reading the data back (we could
		 * potentially use it, if the current write would have felt
		 * before rdev->recovery_offset, but for simplicity we don't
		 * check this here.
		 */
		if (test_bit(In_sync, &rdev->flags) &&
		    !test_bit(Faulty, &rdev->flags))
			set_bit(R1BIO_Uptodate, &r1_bio->state);

		/* Maybe we can clear some bad blocks. */
		if (rdev_has_badblock(rdev, r1_bio->sector, r1_bio->sectors) &&
		    !discard_error) {
			r1_bio->bios[mirror] = IO_MADE_GOOD;
			set_bit(R1BIO_MadeGood, &r1_bio->state);
		}
	}

	if (behind) {
		if (test_bit(CollisionCheck, &rdev->flags))
			remove_serial(rdev, lo, hi);
		if (test_bit(WriteMostly, &rdev->flags))
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
				pr_debug("raid1: behind end write sectors"
					 " %llu-%llu\n",
					 (unsigned long long) mbio->bi_iter.bi_sector,
					 (unsigned long long) bio_end_sector(mbio) - 1);
				call_bio_endio(r1_bio);
			}
		}
	} else if (rdev->mddev->serialize_policy)
		remove_serial(rdev, lo, hi);
	if (r1_bio->bios[mirror] == NULL)
		rdev_dec_pending(rdev, conf->mddev);

	/*
	 * Let's see if all mirrored write operations have finished
	 * already.
	 */
	r1_bio_write_done(r1_bio);

	if (to_put)
		bio_put(to_put);
}

static sector_t align_to_barrier_unit_end(sector_t start_sector,
					  sector_t sectors)
{
	sector_t len;

	WARN_ON(sectors == 0);
	/*
	 * len is the number of sectors from start_sector to end of the
	 * barrier unit which start_sector belongs to.
	 */
	len = round_up(start_sector + 1, BARRIER_UNIT_SECTOR_SIZE) -
	      start_sector;

	if (len > sectors)
		len = sectors;

	return len;
}

static void update_read_sectors(struct r1conf *conf, int disk,
				sector_t this_sector, int len)
{
	struct raid1_info *info = &conf->mirrors[disk];

	atomic_inc(&info->rdev->nr_pending);
	if (info->next_seq_sect != this_sector)
		info->seq_start = this_sector;
	info->next_seq_sect = this_sector + len;
}

static int choose_first_rdev(struct r1conf *conf, struct r1bio *r1_bio,
			     int *max_sectors)
{
	sector_t this_sector = r1_bio->sector;
	int len = r1_bio->sectors;
	int disk;

	for (disk = 0 ; disk < conf->raid_disks * 2 ; disk++) {
		struct md_rdev *rdev;
		int read_len;

		if (r1_bio->bios[disk] == IO_BLOCKED)
			continue;

		rdev = conf->mirrors[disk].rdev;
		if (!rdev || test_bit(Faulty, &rdev->flags))
			continue;

		/* choose the first disk even if it has some bad blocks. */
		read_len = raid1_check_read_range(rdev, this_sector, &len);
		if (read_len > 0) {
			update_read_sectors(conf, disk, this_sector, read_len);
			*max_sectors = read_len;
			return disk;
		}
	}

	return -1;
}

static bool rdev_in_recovery(struct md_rdev *rdev, struct r1bio *r1_bio)
{
	return !test_bit(In_sync, &rdev->flags) &&
	       rdev->recovery_offset < r1_bio->sector + r1_bio->sectors;
}

static int choose_bb_rdev(struct r1conf *conf, struct r1bio *r1_bio,
			  int *max_sectors)
{
	sector_t this_sector = r1_bio->sector;
	int best_disk = -1;
	int best_len = 0;
	int disk;

	for (disk = 0 ; disk < conf->raid_disks * 2 ; disk++) {
		struct md_rdev *rdev;
		int len;
		int read_len;

		if (r1_bio->bios[disk] == IO_BLOCKED)
			continue;

		rdev = conf->mirrors[disk].rdev;
		if (!rdev || test_bit(Faulty, &rdev->flags) ||
		    rdev_in_recovery(rdev, r1_bio) ||
		    test_bit(WriteMostly, &rdev->flags))
			continue;

		/* keep track of the disk with the most readable sectors. */
		len = r1_bio->sectors;
		read_len = raid1_check_read_range(rdev, this_sector, &len);
		if (read_len > best_len) {
			best_disk = disk;
			best_len = read_len;
		}
	}

	if (best_disk != -1) {
		*max_sectors = best_len;
		update_read_sectors(conf, best_disk, this_sector, best_len);
	}

	return best_disk;
}

static int choose_slow_rdev(struct r1conf *conf, struct r1bio *r1_bio,
			    int *max_sectors)
{
	sector_t this_sector = r1_bio->sector;
	int bb_disk = -1;
	int bb_read_len = 0;
	int disk;

	for (disk = 0 ; disk < conf->raid_disks * 2 ; disk++) {
		struct md_rdev *rdev;
		int len;
		int read_len;

		if (r1_bio->bios[disk] == IO_BLOCKED)
			continue;

		rdev = conf->mirrors[disk].rdev;
		if (!rdev || test_bit(Faulty, &rdev->flags) ||
		    !test_bit(WriteMostly, &rdev->flags) ||
		    rdev_in_recovery(rdev, r1_bio))
			continue;

		/* there are no bad blocks, we can use this disk */
		len = r1_bio->sectors;
		read_len = raid1_check_read_range(rdev, this_sector, &len);
		if (read_len == r1_bio->sectors) {
			*max_sectors = read_len;
			update_read_sectors(conf, disk, this_sector, read_len);
			return disk;
		}

		/*
		 * there are partial bad blocks, choose the rdev with largest
		 * read length.
		 */
		if (read_len > bb_read_len) {
			bb_disk = disk;
			bb_read_len = read_len;
		}
	}

	if (bb_disk != -1) {
		*max_sectors = bb_read_len;
		update_read_sectors(conf, bb_disk, this_sector, bb_read_len);
	}

	return bb_disk;
}

static bool is_sequential(struct r1conf *conf, int disk, struct r1bio *r1_bio)
{
	/* TODO: address issues with this check and concurrency. */
	return conf->mirrors[disk].next_seq_sect == r1_bio->sector ||
	       conf->mirrors[disk].head_position == r1_bio->sector;
}

/*
 * If buffered sequential IO size exceeds optimal iosize, check if there is idle
 * disk. If yes, choose the idle disk.
 */
static bool should_choose_next(struct r1conf *conf, int disk)
{
	struct raid1_info *mirror = &conf->mirrors[disk];
	int opt_iosize;

	if (!test_bit(Nonrot, &mirror->rdev->flags))
		return false;

	opt_iosize = bdev_io_opt(mirror->rdev->bdev) >> 9;
	return opt_iosize > 0 && mirror->seq_start != MaxSector &&
	       mirror->next_seq_sect > opt_iosize &&
	       mirror->next_seq_sect - opt_iosize >= mirror->seq_start;
}

static bool rdev_readable(struct md_rdev *rdev, struct r1bio *r1_bio)
{
	if (!rdev || test_bit(Faulty, &rdev->flags))
		return false;

	if (rdev_in_recovery(rdev, r1_bio))
		return false;

	/* don't read from slow disk unless have to */
	if (test_bit(WriteMostly, &rdev->flags))
		return false;

	/* don't split IO for bad blocks unless have to */
	if (rdev_has_badblock(rdev, r1_bio->sector, r1_bio->sectors))
		return false;

	return true;
}

struct read_balance_ctl {
	sector_t closest_dist;
	int closest_dist_disk;
	int min_pending;
	int min_pending_disk;
	int sequential_disk;
	int readable_disks;
};

static int choose_best_rdev(struct r1conf *conf, struct r1bio *r1_bio)
{
	int disk;
	struct read_balance_ctl ctl = {
		.closest_dist_disk      = -1,
		.closest_dist           = MaxSector,
		.min_pending_disk       = -1,
		.min_pending            = UINT_MAX,
		.sequential_disk	= -1,
	};

	for (disk = 0 ; disk < conf->raid_disks * 2 ; disk++) {
		struct md_rdev *rdev;
		sector_t dist;
		unsigned int pending;

		if (r1_bio->bios[disk] == IO_BLOCKED)
			continue;

		rdev = conf->mirrors[disk].rdev;
		if (!rdev_readable(rdev, r1_bio))
			continue;

		/* At least two disks to choose from so failfast is OK */
		if (ctl.readable_disks++ == 1)
			set_bit(R1BIO_FailFast, &r1_bio->state);

		pending = atomic_read(&rdev->nr_pending);
		dist = abs(r1_bio->sector - conf->mirrors[disk].head_position);

		/* Don't change to another disk for sequential reads */
		if (is_sequential(conf, disk, r1_bio)) {
			if (!should_choose_next(conf, disk))
				return disk;

			/*
			 * Add 'pending' to avoid choosing this disk if
			 * there is other idle disk.
			 */
			pending++;
			/*
			 * If there is no other idle disk, this disk
			 * will be chosen.
			 */
			ctl.sequential_disk = disk;
		}

		if (ctl.min_pending > pending) {
			ctl.min_pending = pending;
			ctl.min_pending_disk = disk;
		}

		if (ctl.closest_dist > dist) {
			ctl.closest_dist = dist;
			ctl.closest_dist_disk = disk;
		}
	}

	/*
	 * sequential IO size exceeds optimal iosize, however, there is no other
	 * idle disk, so choose the sequential disk.
	 */
	if (ctl.sequential_disk != -1 && ctl.min_pending != 0)
		return ctl.sequential_disk;

	/*
	 * If all disks are rotational, choose the closest disk. If any disk is
	 * non-rotational, choose the disk with less pending request even the
	 * disk is rotational, which might/might not be optimal for raids with
	 * mixed ratation/non-rotational disks depending on workload.
	 */
	if (ctl.min_pending_disk != -1 &&
	    (READ_ONCE(conf->nonrot_disks) || ctl.min_pending == 0))
		return ctl.min_pending_disk;
	else
		return ctl.closest_dist_disk;
}

/*
 * This routine returns the disk from which the requested read should be done.
 *
 * 1) If resync is in progress, find the first usable disk and use it even if it
 * has some bad blocks.
 *
 * 2) Now that there is no resync, loop through all disks and skipping slow
 * disks and disks with bad blocks for now. Only pay attention to key disk
 * choice.
 *
 * 3) If we've made it this far, now look for disks with bad blocks and choose
 * the one with most number of sectors.
 *
 * 4) If we are all the way at the end, we have no choice but to use a disk even
 * if it is write mostly.
 *
 * The rdev for the device selected will have nr_pending incremented.
 */
static int read_balance(struct r1conf *conf, struct r1bio *r1_bio,
			int *max_sectors)
{
	int disk;

	clear_bit(R1BIO_FailFast, &r1_bio->state);

	if (raid1_should_read_first(conf->mddev, r1_bio->sector,
				    r1_bio->sectors))
		return choose_first_rdev(conf, r1_bio, max_sectors);

	disk = choose_best_rdev(conf, r1_bio);
	if (disk >= 0) {
		*max_sectors = r1_bio->sectors;
		update_read_sectors(conf, disk, r1_bio->sector,
				    r1_bio->sectors);
		return disk;
	}

	/*
	 * If we are here it means we didn't find a perfectly good disk so
	 * now spend a bit more time trying to find one with the most good
	 * sectors.
	 */
	disk = choose_bb_rdev(conf, r1_bio, max_sectors);
	if (disk >= 0)
		return disk;

	return choose_slow_rdev(conf, r1_bio, max_sectors);
}

static void wake_up_barrier(struct r1conf *conf)
{
	if (wq_has_sleeper(&conf->wait_barrier))
		wake_up(&conf->wait_barrier);
}

static void flush_bio_list(struct r1conf *conf, struct bio *bio)
{
	/* flush any pending bitmap writes to disk before proceeding w/ I/O */
	raid1_prepare_flush_writes(conf->mddev);
	wake_up_barrier(conf);

	while (bio) { /* submit pending writes */
		struct bio *next = bio->bi_next;

		raid1_submit_write(bio);
		bio = next;
		cond_resched();
	}
}

static void flush_pending_writes(struct r1conf *conf)
{
	/* Any writes that have been queued but are awaiting
	 * bitmap updates get flushed here.
	 */
	spin_lock_irq(&conf->device_lock);

	if (conf->pending_bio_list.head) {
		struct blk_plug plug;
		struct bio *bio;

		bio = bio_list_get(&conf->pending_bio_list);
		spin_unlock_irq(&conf->device_lock);

		/*
		 * As this is called in a wait_event() loop (see freeze_array),
		 * current->state might be TASK_UNINTERRUPTIBLE which will
		 * cause a warning when we prepare to wait again.  As it is
		 * rare that this path is taken, it is perfectly safe to force
		 * us to go around the wait_event() loop again, so the warning
		 * is a false-positive.  Silence the warning by resetting
		 * thread state
		 */
		__set_current_state(TASK_RUNNING);
		blk_start_plug(&plug);
		flush_bio_list(conf, bio);
		blk_finish_plug(&plug);
	} else
		spin_unlock_irq(&conf->device_lock);
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
 *
 * If resync/recovery is interrupted, returns -EINTR;
 * Otherwise, returns 0.
 */
static int raise_barrier(struct r1conf *conf, sector_t sector_nr)
{
	int idx = sector_to_idx(sector_nr);

	spin_lock_irq(&conf->resync_lock);

	/* Wait until no block IO is waiting */
	wait_event_lock_irq(conf->wait_barrier,
			    !atomic_read(&conf->nr_waiting[idx]),
			    conf->resync_lock);

	/* block any new IO from starting */
	atomic_inc(&conf->barrier[idx]);
	/*
	 * In raise_barrier() we firstly increase conf->barrier[idx] then
	 * check conf->nr_pending[idx]. In _wait_barrier() we firstly
	 * increase conf->nr_pending[idx] then check conf->barrier[idx].
	 * A memory barrier here to make sure conf->nr_pending[idx] won't
	 * be fetched before conf->barrier[idx] is increased. Otherwise
	 * there will be a race between raise_barrier() and _wait_barrier().
	 */
	smp_mb__after_atomic();

	/* For these conditions we must wait:
	 * A: while the array is in frozen state
	 * B: while conf->nr_pending[idx] is not 0, meaning regular I/O
	 *    existing in corresponding I/O barrier bucket.
	 * C: while conf->barrier[idx] >= RESYNC_DEPTH, meaning reaches
	 *    max resync count which allowed on current I/O barrier bucket.
	 */
	wait_event_lock_irq(conf->wait_barrier,
			    (!conf->array_frozen &&
			     !atomic_read(&conf->nr_pending[idx]) &&
			     atomic_read(&conf->barrier[idx]) < RESYNC_DEPTH) ||
				test_bit(MD_RECOVERY_INTR, &conf->mddev->recovery),
			    conf->resync_lock);

	if (test_bit(MD_RECOVERY_INTR, &conf->mddev->recovery)) {
		atomic_dec(&conf->barrier[idx]);
		spin_unlock_irq(&conf->resync_lock);
		wake_up(&conf->wait_barrier);
		return -EINTR;
	}

	atomic_inc(&conf->nr_sync_pending);
	spin_unlock_irq(&conf->resync_lock);

	return 0;
}

static void lower_barrier(struct r1conf *conf, sector_t sector_nr)
{
	int idx = sector_to_idx(sector_nr);

	BUG_ON(atomic_read(&conf->barrier[idx]) <= 0);

	atomic_dec(&conf->barrier[idx]);
	atomic_dec(&conf->nr_sync_pending);
	wake_up(&conf->wait_barrier);
}

static bool _wait_barrier(struct r1conf *conf, int idx, bool nowait)
{
	bool ret = true;

	/*
	 * We need to increase conf->nr_pending[idx] very early here,
	 * then raise_barrier() can be blocked when it waits for
	 * conf->nr_pending[idx] to be 0. Then we can avoid holding
	 * conf->resync_lock when there is no barrier raised in same
	 * barrier unit bucket. Also if the array is frozen, I/O
	 * should be blocked until array is unfrozen.
	 */
	atomic_inc(&conf->nr_pending[idx]);
	/*
	 * In _wait_barrier() we firstly increase conf->nr_pending[idx], then
	 * check conf->barrier[idx]. In raise_barrier() we firstly increase
	 * conf->barrier[idx], then check conf->nr_pending[idx]. A memory
	 * barrier is necessary here to make sure conf->barrier[idx] won't be
	 * fetched before conf->nr_pending[idx] is increased. Otherwise there
	 * will be a race between _wait_barrier() and raise_barrier().
	 */
	smp_mb__after_atomic();

	/*
	 * Don't worry about checking two atomic_t variables at same time
	 * here. If during we check conf->barrier[idx], the array is
	 * frozen (conf->array_frozen is 1), and chonf->barrier[idx] is
	 * 0, it is safe to return and make the I/O continue. Because the
	 * array is frozen, all I/O returned here will eventually complete
	 * or be queued, no race will happen. See code comment in
	 * frozen_array().
	 */
	if (!READ_ONCE(conf->array_frozen) &&
	    !atomic_read(&conf->barrier[idx]))
		return ret;

	/*
	 * After holding conf->resync_lock, conf->nr_pending[idx]
	 * should be decreased before waiting for barrier to drop.
	 * Otherwise, we may encounter a race condition because
	 * raise_barrer() might be waiting for conf->nr_pending[idx]
	 * to be 0 at same time.
	 */
	spin_lock_irq(&conf->resync_lock);
	atomic_inc(&conf->nr_waiting[idx]);
	atomic_dec(&conf->nr_pending[idx]);
	/*
	 * In case freeze_array() is waiting for
	 * get_unqueued_pending() == extra
	 */
	wake_up_barrier(conf);
	/* Wait for the barrier in same barrier unit bucket to drop. */

	/* Return false when nowait flag is set */
	if (nowait) {
		ret = false;
	} else {
		wait_event_lock_irq(conf->wait_barrier,
				!conf->array_frozen &&
				!atomic_read(&conf->barrier[idx]),
				conf->resync_lock);
		atomic_inc(&conf->nr_pending[idx]);
	}

	atomic_dec(&conf->nr_waiting[idx]);
	spin_unlock_irq(&conf->resync_lock);
	return ret;
}

static bool wait_read_barrier(struct r1conf *conf, sector_t sector_nr, bool nowait)
{
	int idx = sector_to_idx(sector_nr);
	bool ret = true;

	/*
	 * Very similar to _wait_barrier(). The difference is, for read
	 * I/O we don't need wait for sync I/O, but if the whole array
	 * is frozen, the read I/O still has to wait until the array is
	 * unfrozen. Since there is no ordering requirement with
	 * conf->barrier[idx] here, memory barrier is unnecessary as well.
	 */
	atomic_inc(&conf->nr_pending[idx]);

	if (!READ_ONCE(conf->array_frozen))
		return ret;

	spin_lock_irq(&conf->resync_lock);
	atomic_inc(&conf->nr_waiting[idx]);
	atomic_dec(&conf->nr_pending[idx]);
	/*
	 * In case freeze_array() is waiting for
	 * get_unqueued_pending() == extra
	 */
	wake_up_barrier(conf);
	/* Wait for array to be unfrozen */

	/* Return false when nowait flag is set */
	if (nowait) {
		/* Return false when nowait flag is set */
		ret = false;
	} else {
		wait_event_lock_irq(conf->wait_barrier,
				!conf->array_frozen,
				conf->resync_lock);
		atomic_inc(&conf->nr_pending[idx]);
	}

	atomic_dec(&conf->nr_waiting[idx]);
	spin_unlock_irq(&conf->resync_lock);
	return ret;
}

static bool wait_barrier(struct r1conf *conf, sector_t sector_nr, bool nowait)
{
	int idx = sector_to_idx(sector_nr);

	return _wait_barrier(conf, idx, nowait);
}

static void _allow_barrier(struct r1conf *conf, int idx)
{
	atomic_dec(&conf->nr_pending[idx]);
	wake_up_barrier(conf);
}

static void allow_barrier(struct r1conf *conf, sector_t sector_nr)
{
	int idx = sector_to_idx(sector_nr);

	_allow_barrier(conf, idx);
}

/* conf->resync_lock should be held */
static int get_unqueued_pending(struct r1conf *conf)
{
	int idx, ret;

	ret = atomic_read(&conf->nr_sync_pending);
	for (idx = 0; idx < BARRIER_BUCKETS_NR; idx++)
		ret += atomic_read(&conf->nr_pending[idx]) -
			atomic_read(&conf->nr_queued[idx]);

	return ret;
}

static void freeze_array(struct r1conf *conf, int extra)
{
	/* Stop sync I/O and normal I/O and wait for everything to
	 * go quiet.
	 * This is called in two situations:
	 * 1) management command handlers (reshape, remove disk, quiesce).
	 * 2) one normal I/O request failed.

	 * After array_frozen is set to 1, new sync IO will be blocked at
	 * raise_barrier(), and new normal I/O will blocked at _wait_barrier()
	 * or wait_read_barrier(). The flying I/Os will either complete or be
	 * queued. When everything goes quite, there are only queued I/Os left.

	 * Every flying I/O contributes to a conf->nr_pending[idx], idx is the
	 * barrier bucket index which this I/O request hits. When all sync and
	 * normal I/O are queued, sum of all conf->nr_pending[] will match sum
	 * of all conf->nr_queued[]. But normal I/O failure is an exception,
	 * in handle_read_error(), we may call freeze_array() before trying to
	 * fix the read error. In this case, the error read I/O is not queued,
	 * so get_unqueued_pending() == 1.
	 *
	 * Therefore before this function returns, we need to wait until
	 * get_unqueued_pendings(conf) gets equal to extra. For
	 * normal I/O context, extra is 1, in rested situations extra is 0.
	 */
	spin_lock_irq(&conf->resync_lock);
	conf->array_frozen = 1;
	mddev_add_trace_msg(conf->mddev, "raid1 wait freeze");
	wait_event_lock_irq_cmd(
		conf->wait_barrier,
		get_unqueued_pending(conf) == extra,
		conf->resync_lock,
		flush_pending_writes(conf));
	spin_unlock_irq(&conf->resync_lock);
}
static void unfreeze_array(struct r1conf *conf)
{
	/* reverse the effect of the freeze */
	spin_lock_irq(&conf->resync_lock);
	conf->array_frozen = 0;
	spin_unlock_irq(&conf->resync_lock);
	wake_up(&conf->wait_barrier);
}

static void alloc_behind_master_bio(struct r1bio *r1_bio,
					   struct bio *bio)
{
	int size = bio->bi_iter.bi_size;
	unsigned vcnt = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	int i = 0;
	struct bio *behind_bio = NULL;

	behind_bio = bio_alloc_bioset(NULL, vcnt, 0, GFP_NOIO,
				      &r1_bio->mddev->bio_set);

	/* discard op, we don't support writezero/writesame yet */
	if (!bio_has_data(bio)) {
		behind_bio->bi_iter.bi_size = size;
		goto skip_copy;
	}

	while (i < vcnt && size) {
		struct page *page;
		int len = min_t(int, PAGE_SIZE, size);

		page = alloc_page(GFP_NOIO);
		if (unlikely(!page))
			goto free_pages;

		if (!bio_add_page(behind_bio, page, len, 0)) {
			put_page(page);
			goto free_pages;
		}

		size -= len;
		i++;
	}

	bio_copy_data(behind_bio, bio);
skip_copy:
	r1_bio->behind_master_bio = behind_bio;
	set_bit(R1BIO_BehindIO, &r1_bio->state);

	return;

free_pages:
	pr_debug("%dB behind alloc failed, doing sync I/O\n",
		 bio->bi_iter.bi_size);
	bio_free_pages(behind_bio);
	bio_put(behind_bio);
}

static void raid1_unplug(struct blk_plug_cb *cb, bool from_schedule)
{
	struct raid1_plug_cb *plug = container_of(cb, struct raid1_plug_cb,
						  cb);
	struct mddev *mddev = plug->cb.data;
	struct r1conf *conf = mddev->private;
	struct bio *bio;

	if (from_schedule) {
		spin_lock_irq(&conf->device_lock);
		bio_list_merge(&conf->pending_bio_list, &plug->pending);
		spin_unlock_irq(&conf->device_lock);
		wake_up_barrier(conf);
		md_wakeup_thread(mddev->thread);
		kfree(plug);
		return;
	}

	/* we aren't scheduling, so we can do the write-out directly. */
	bio = bio_list_get(&plug->pending);
	flush_bio_list(conf, bio);
	kfree(plug);
}

static void init_r1bio(struct r1bio *r1_bio, struct mddev *mddev, struct bio *bio)
{
	r1_bio->master_bio = bio;
	r1_bio->sectors = bio_sectors(bio);
	r1_bio->state = 0;
	r1_bio->mddev = mddev;
	r1_bio->sector = bio->bi_iter.bi_sector;
}

static inline struct r1bio *
alloc_r1bio(struct mddev *mddev, struct bio *bio)
{
	struct r1conf *conf = mddev->private;
	struct r1bio *r1_bio;

	r1_bio = mempool_alloc(&conf->r1bio_pool, GFP_NOIO);
	/* Ensure no bio records IO_BLOCKED */
	memset(r1_bio->bios, 0, conf->raid_disks * sizeof(r1_bio->bios[0]));
	init_r1bio(r1_bio, mddev, bio);
	return r1_bio;
}

static void raid1_read_request(struct mddev *mddev, struct bio *bio,
			       int max_read_sectors, struct r1bio *r1_bio)
{
	struct r1conf *conf = mddev->private;
	struct raid1_info *mirror;
	struct bio *read_bio;
	const enum req_op op = bio_op(bio);
	const blk_opf_t do_sync = bio->bi_opf & REQ_SYNC;
	int max_sectors;
	int rdisk, error;
	bool r1bio_existed = !!r1_bio;

	/*
	 * If r1_bio is set, we are blocking the raid1d thread
	 * so there is a tiny risk of deadlock.  So ask for
	 * emergency memory if needed.
	 */
	gfp_t gfp = r1_bio ? (GFP_NOIO | __GFP_HIGH) : GFP_NOIO;

	/*
	 * Still need barrier for READ in case that whole
	 * array is frozen.
	 */
	if (!wait_read_barrier(conf, bio->bi_iter.bi_sector,
				bio->bi_opf & REQ_NOWAIT)) {
		bio_wouldblock_error(bio);
		return;
	}

	if (!r1_bio)
		r1_bio = alloc_r1bio(mddev, bio);
	else
		init_r1bio(r1_bio, mddev, bio);
	r1_bio->sectors = max_read_sectors;

	/*
	 * make_request() can abort the operation when read-ahead is being
	 * used and no empty request is available.
	 */
	rdisk = read_balance(conf, r1_bio, &max_sectors);
	if (rdisk < 0) {
		/* couldn't find anywhere to read from */
		if (r1bio_existed)
			pr_crit_ratelimited("md/raid1:%s: %pg: unrecoverable I/O read error for block %llu\n",
					    mdname(mddev),
					    conf->mirrors[r1_bio->read_disk].rdev->bdev,
					    r1_bio->sector);
		raid_end_bio_io(r1_bio);
		return;
	}
	mirror = conf->mirrors + rdisk;

	if (r1bio_existed)
		pr_info_ratelimited("md/raid1:%s: redirecting sector %llu to other mirror: %pg\n",
				    mdname(mddev),
				    (unsigned long long)r1_bio->sector,
				    mirror->rdev->bdev);

	if (test_bit(WriteMostly, &mirror->rdev->flags)) {
		/*
		 * Reading from a write-mostly device must take care not to
		 * over-take any writes that are 'behind'
		 */
		mddev_add_trace_msg(mddev, "raid1 wait behind writes");
		mddev->bitmap_ops->wait_behind_writes(mddev);
	}

	if (max_sectors < bio_sectors(bio)) {
		struct bio *split = bio_split(bio, max_sectors,
					      gfp, &conf->bio_split);

		if (IS_ERR(split)) {
			error = PTR_ERR(split);
			goto err_handle;
		}
		bio_chain(split, bio);
		submit_bio_noacct(bio);
		bio = split;
		r1_bio->master_bio = bio;
		r1_bio->sectors = max_sectors;
	}

	r1_bio->read_disk = rdisk;
	if (!r1bio_existed) {
		md_account_bio(mddev, &bio);
		r1_bio->master_bio = bio;
	}
	read_bio = bio_alloc_clone(mirror->rdev->bdev, bio, gfp,
				   &mddev->bio_set);

	r1_bio->bios[rdisk] = read_bio;

	read_bio->bi_iter.bi_sector = r1_bio->sector +
		mirror->rdev->data_offset;
	read_bio->bi_end_io = raid1_end_read_request;
	read_bio->bi_opf = op | do_sync;
	if (test_bit(FailFast, &mirror->rdev->flags) &&
	    test_bit(R1BIO_FailFast, &r1_bio->state))
	        read_bio->bi_opf |= MD_FAILFAST;
	read_bio->bi_private = r1_bio;
	mddev_trace_remap(mddev, read_bio, r1_bio->sector);
	submit_bio_noacct(read_bio);
	return;

err_handle:
	atomic_dec(&mirror->rdev->nr_pending);
	bio->bi_status = errno_to_blk_status(error);
	set_bit(R1BIO_Uptodate, &r1_bio->state);
	raid_end_bio_io(r1_bio);
}

static bool wait_blocked_rdev(struct mddev *mddev, struct bio *bio)
{
	struct r1conf *conf = mddev->private;
	int disks = conf->raid_disks * 2;
	int i;

retry:
	for (i = 0; i < disks; i++) {
		struct md_rdev *rdev = conf->mirrors[i].rdev;

		if (!rdev)
			continue;

		/* don't write here until the bad block is acknowledged */
		if (test_bit(WriteErrorSeen, &rdev->flags) &&
		    rdev_has_badblock(rdev, bio->bi_iter.bi_sector,
				      bio_sectors(bio)) < 0)
			set_bit(BlockedBadBlocks, &rdev->flags);

		if (rdev_blocked(rdev)) {
			if (bio->bi_opf & REQ_NOWAIT)
				return false;

			mddev_add_trace_msg(rdev->mddev, "raid1 wait rdev %d blocked",
					    rdev->raid_disk);
			atomic_inc(&rdev->nr_pending);
			md_wait_for_blocked_rdev(rdev, rdev->mddev);
			goto retry;
		}
	}

	return true;
}

static void raid1_write_request(struct mddev *mddev, struct bio *bio,
				int max_write_sectors)
{
	struct r1conf *conf = mddev->private;
	struct r1bio *r1_bio;
	int i, disks, k, error;
	unsigned long flags;
	int first_clone;
	int max_sectors;
	bool write_behind = false;
	bool is_discard = (bio_op(bio) == REQ_OP_DISCARD);

	if (mddev_is_clustered(mddev) &&
	     md_cluster_ops->area_resyncing(mddev, WRITE,
		     bio->bi_iter.bi_sector, bio_end_sector(bio))) {

		DEFINE_WAIT(w);
		if (bio->bi_opf & REQ_NOWAIT) {
			bio_wouldblock_error(bio);
			return;
		}
		for (;;) {
			prepare_to_wait(&conf->wait_barrier,
					&w, TASK_IDLE);
			if (!md_cluster_ops->area_resyncing(mddev, WRITE,
							bio->bi_iter.bi_sector,
							bio_end_sector(bio)))
				break;
			schedule();
		}
		finish_wait(&conf->wait_barrier, &w);
	}

	/*
	 * Register the new request and wait if the reconstruction
	 * thread has put up a bar for new requests.
	 * Continue immediately if no resync is active currently.
	 */
	if (!wait_barrier(conf, bio->bi_iter.bi_sector,
				bio->bi_opf & REQ_NOWAIT)) {
		bio_wouldblock_error(bio);
		return;
	}

	if (!wait_blocked_rdev(mddev, bio)) {
		bio_wouldblock_error(bio);
		return;
	}

	r1_bio = alloc_r1bio(mddev, bio);
	r1_bio->sectors = max_write_sectors;

	/* first select target devices under rcu_lock and
	 * inc refcount on their rdev.  Record them by setting
	 * bios[x] to bio
	 * If there are known/acknowledged bad blocks on any device on
	 * which we have seen a write error, we want to avoid writing those
	 * blocks.
	 * This potentially requires several writes to write around
	 * the bad blocks.  Each set of writes gets it's own r1bio
	 * with a set of bios attached.
	 */

	disks = conf->raid_disks * 2;
	max_sectors = r1_bio->sectors;
	for (i = 0;  i < disks; i++) {
		struct md_rdev *rdev = conf->mirrors[i].rdev;

		/*
		 * The write-behind io is only attempted on drives marked as
		 * write-mostly, which means we could allocate write behind
		 * bio later.
		 */
		if (!is_discard && rdev && test_bit(WriteMostly, &rdev->flags))
			write_behind = true;

		r1_bio->bios[i] = NULL;
		if (!rdev || test_bit(Faulty, &rdev->flags))
			continue;

		atomic_inc(&rdev->nr_pending);
		if (test_bit(WriteErrorSeen, &rdev->flags)) {
			sector_t first_bad;
			int bad_sectors;
			int is_bad;

			is_bad = is_badblock(rdev, r1_bio->sector, max_sectors,
					     &first_bad, &bad_sectors);
			if (is_bad && first_bad <= r1_bio->sector) {
				/* Cannot write here at all */
				bad_sectors -= (r1_bio->sector - first_bad);
				if (bad_sectors < max_sectors)
					/* mustn't write more than bad_sectors
					 * to other devices yet
					 */
					max_sectors = bad_sectors;
				rdev_dec_pending(rdev, mddev);
				continue;
			}
			if (is_bad) {
				int good_sectors;

				/*
				 * We cannot atomically write this, so just
				 * error in that case. It could be possible to
				 * atomically write other mirrors, but the
				 * complexity of supporting that is not worth
				 * the benefit.
				 */
				if (bio->bi_opf & REQ_ATOMIC) {
					error = -EIO;
					goto err_handle;
				}

				good_sectors = first_bad - r1_bio->sector;
				if (good_sectors < max_sectors)
					max_sectors = good_sectors;
			}
		}
		r1_bio->bios[i] = bio;
	}

	/*
	 * When using a bitmap, we may call alloc_behind_master_bio below.
	 * alloc_behind_master_bio allocates a copy of the data payload a page
	 * at a time and thus needs a new bio that can fit the whole payload
	 * this bio in page sized chunks.
	 */
	if (write_behind && mddev->bitmap)
		max_sectors = min_t(int, max_sectors,
				    BIO_MAX_VECS * (PAGE_SIZE >> 9));
	if (max_sectors < bio_sectors(bio)) {
		struct bio *split = bio_split(bio, max_sectors,
					      GFP_NOIO, &conf->bio_split);

		if (IS_ERR(split)) {
			error = PTR_ERR(split);
			goto err_handle;
		}
		bio_chain(split, bio);
		submit_bio_noacct(bio);
		bio = split;
		r1_bio->master_bio = bio;
		r1_bio->sectors = max_sectors;
	}

	md_account_bio(mddev, &bio);
	r1_bio->master_bio = bio;
	atomic_set(&r1_bio->remaining, 1);
	atomic_set(&r1_bio->behind_remaining, 0);

	first_clone = 1;

	for (i = 0; i < disks; i++) {
		struct bio *mbio = NULL;
		struct md_rdev *rdev = conf->mirrors[i].rdev;
		if (!r1_bio->bios[i])
			continue;

		if (first_clone) {
			unsigned long max_write_behind =
				mddev->bitmap_info.max_write_behind;
			struct md_bitmap_stats stats;
			int err;

			/* do behind I/O ?
			 * Not if there are too many, or cannot
			 * allocate memory, or a reader on WriteMostly
			 * is waiting for behind writes to flush */
			err = mddev->bitmap_ops->get_stats(mddev->bitmap, &stats);
			if (!err && write_behind && !stats.behind_wait &&
			    stats.behind_writes < max_write_behind)
				alloc_behind_master_bio(r1_bio, bio);

			if (test_bit(R1BIO_BehindIO, &r1_bio->state))
				mddev->bitmap_ops->start_behind_write(mddev);
			first_clone = 0;
		}

		if (r1_bio->behind_master_bio) {
			mbio = bio_alloc_clone(rdev->bdev,
					       r1_bio->behind_master_bio,
					       GFP_NOIO, &mddev->bio_set);
			if (test_bit(CollisionCheck, &rdev->flags))
				wait_for_serialization(rdev, r1_bio);
			if (test_bit(WriteMostly, &rdev->flags))
				atomic_inc(&r1_bio->behind_remaining);
		} else {
			mbio = bio_alloc_clone(rdev->bdev, bio, GFP_NOIO,
					       &mddev->bio_set);

			if (mddev->serialize_policy)
				wait_for_serialization(rdev, r1_bio);
		}

		r1_bio->bios[i] = mbio;

		mbio->bi_iter.bi_sector	= (r1_bio->sector + rdev->data_offset);
		mbio->bi_end_io	= raid1_end_write_request;
		mbio->bi_opf = bio_op(bio) |
			(bio->bi_opf & (REQ_SYNC | REQ_FUA | REQ_ATOMIC));
		if (test_bit(FailFast, &rdev->flags) &&
		    !test_bit(WriteMostly, &rdev->flags) &&
		    conf->raid_disks - mddev->degraded > 1)
			mbio->bi_opf |= MD_FAILFAST;
		mbio->bi_private = r1_bio;

		atomic_inc(&r1_bio->remaining);
		mddev_trace_remap(mddev, mbio, r1_bio->sector);
		/* flush_pending_writes() needs access to the rdev so...*/
		mbio->bi_bdev = (void *)rdev;
		if (!raid1_add_bio_to_plug(mddev, mbio, raid1_unplug, disks)) {
			spin_lock_irqsave(&conf->device_lock, flags);
			bio_list_add(&conf->pending_bio_list, mbio);
			spin_unlock_irqrestore(&conf->device_lock, flags);
			md_wakeup_thread(mddev->thread);
		}
	}

	r1_bio_write_done(r1_bio);

	/* In case raid1d snuck in to freeze_array */
	wake_up_barrier(conf);
	return;
err_handle:
	for (k = 0; k < i; k++) {
		if (r1_bio->bios[k]) {
			rdev_dec_pending(conf->mirrors[k].rdev, mddev);
			r1_bio->bios[k] = NULL;
		}
	}

	bio->bi_status = errno_to_blk_status(error);
	set_bit(R1BIO_Uptodate, &r1_bio->state);
	raid_end_bio_io(r1_bio);
}

static bool raid1_make_request(struct mddev *mddev, struct bio *bio)
{
	sector_t sectors;

	if (unlikely(bio->bi_opf & REQ_PREFLUSH)
	    && md_flush_request(mddev, bio))
		return true;

	/*
	 * There is a limit to the maximum size, but
	 * the read/write handler might find a lower limit
	 * due to bad blocks.  To avoid multiple splits,
	 * we pass the maximum number of sectors down
	 * and let the lower level perform the split.
	 */
	sectors = align_to_barrier_unit_end(
		bio->bi_iter.bi_sector, bio_sectors(bio));

	if (bio_data_dir(bio) == READ)
		raid1_read_request(mddev, bio, sectors, NULL);
	else {
		md_write_start(mddev,bio);
		raid1_write_request(mddev, bio, sectors);
	}
	return true;
}

static void raid1_status(struct seq_file *seq, struct mddev *mddev)
{
	struct r1conf *conf = mddev->private;
	int i;

	lockdep_assert_held(&mddev->lock);

	seq_printf(seq, " [%d/%d] [", conf->raid_disks,
		   conf->raid_disks - mddev->degraded);
	for (i = 0; i < conf->raid_disks; i++) {
		struct md_rdev *rdev = READ_ONCE(conf->mirrors[i].rdev);

		seq_printf(seq, "%s",
			   rdev && test_bit(In_sync, &rdev->flags) ? "U" : "_");
	}
	seq_printf(seq, "]");
}

/**
 * raid1_error() - RAID1 error handler.
 * @mddev: affected md device.
 * @rdev: member device to fail.
 *
 * The routine acknowledges &rdev failure and determines new @mddev state.
 * If it failed, then:
 *	- &MD_BROKEN flag is set in &mddev->flags.
 *	- recovery is disabled.
 * Otherwise, it must be degraded:
 *	- recovery is interrupted.
 *	- &mddev->degraded is bumped.
 *
 * @rdev is marked as &Faulty excluding case when array is failed and
 * &mddev->fail_last_dev is off.
 */
static void raid1_error(struct mddev *mddev, struct md_rdev *rdev)
{
	struct r1conf *conf = mddev->private;
	unsigned long flags;

	spin_lock_irqsave(&conf->device_lock, flags);

	if (test_bit(In_sync, &rdev->flags) &&
	    (conf->raid_disks - mddev->degraded) == 1) {
		set_bit(MD_BROKEN, &mddev->flags);

		if (!mddev->fail_last_dev) {
			conf->recovery_disabled = mddev->recovery_disabled;
			spin_unlock_irqrestore(&conf->device_lock, flags);
			return;
		}
	}
	set_bit(Blocked, &rdev->flags);
	if (test_and_clear_bit(In_sync, &rdev->flags))
		mddev->degraded++;
	set_bit(Faulty, &rdev->flags);
	spin_unlock_irqrestore(&conf->device_lock, flags);
	/*
	 * if recovery is running, make sure it aborts.
	 */
	set_bit(MD_RECOVERY_INTR, &mddev->recovery);
	set_mask_bits(&mddev->sb_flags, 0,
		      BIT(MD_SB_CHANGE_DEVS) | BIT(MD_SB_CHANGE_PENDING));
	pr_crit("md/raid1:%s: Disk failure on %pg, disabling device.\n"
		"md/raid1:%s: Operation continuing on %d devices.\n",
		mdname(mddev), rdev->bdev,
		mdname(mddev), conf->raid_disks - mddev->degraded);
}

static void print_conf(struct r1conf *conf)
{
	int i;

	pr_debug("RAID1 conf printout:\n");
	if (!conf) {
		pr_debug("(!conf)\n");
		return;
	}
	pr_debug(" --- wd:%d rd:%d\n", conf->raid_disks - conf->mddev->degraded,
		 conf->raid_disks);

	lockdep_assert_held(&conf->mddev->reconfig_mutex);
	for (i = 0; i < conf->raid_disks; i++) {
		struct md_rdev *rdev = conf->mirrors[i].rdev;
		if (rdev)
			pr_debug(" disk %d, wo:%d, o:%d, dev:%pg\n",
				 i, !test_bit(In_sync, &rdev->flags),
				 !test_bit(Faulty, &rdev->flags),
				 rdev->bdev);
	}
}

static void close_sync(struct r1conf *conf)
{
	int idx;

	for (idx = 0; idx < BARRIER_BUCKETS_NR; idx++) {
		_wait_barrier(conf, idx, false);
		_allow_barrier(conf, idx);
	}

	mempool_exit(&conf->r1buf_pool);
}

static int raid1_spare_active(struct mddev *mddev)
{
	int i;
	struct r1conf *conf = mddev->private;
	int count = 0;
	unsigned long flags;

	/*
	 * Find all failed disks within the RAID1 configuration
	 * and mark them readable.
	 * Called under mddev lock, so rcu protection not needed.
	 * device_lock used to avoid races with raid1_end_read_request
	 * which expects 'In_sync' flags and ->degraded to be consistent.
	 */
	spin_lock_irqsave(&conf->device_lock, flags);
	for (i = 0; i < conf->raid_disks; i++) {
		struct md_rdev *rdev = conf->mirrors[i].rdev;
		struct md_rdev *repl = conf->mirrors[conf->raid_disks + i].rdev;
		if (repl
		    && !test_bit(Candidate, &repl->flags)
		    && repl->recovery_offset == MaxSector
		    && !test_bit(Faulty, &repl->flags)
		    && !test_and_set_bit(In_sync, &repl->flags)) {
			/* replacement has just become active */
			if (!rdev ||
			    !test_and_clear_bit(In_sync, &rdev->flags))
				count++;
			if (rdev) {
				/* Replaced device not technically
				 * faulty, but we need to be sure
				 * it gets removed and never re-added
				 */
				set_bit(Faulty, &rdev->flags);
				sysfs_notify_dirent_safe(
					rdev->sysfs_state);
			}
		}
		if (rdev
		    && rdev->recovery_offset == MaxSector
		    && !test_bit(Faulty, &rdev->flags)
		    && !test_and_set_bit(In_sync, &rdev->flags)) {
			count++;
			sysfs_notify_dirent_safe(rdev->sysfs_state);
		}
	}
	mddev->degraded -= count;
	spin_unlock_irqrestore(&conf->device_lock, flags);

	print_conf(conf);
	return count;
}

static bool raid1_add_conf(struct r1conf *conf, struct md_rdev *rdev, int disk,
			   bool replacement)
{
	struct raid1_info *info = conf->mirrors + disk;

	if (replacement)
		info += conf->raid_disks;

	if (info->rdev)
		return false;

	if (bdev_nonrot(rdev->bdev)) {
		set_bit(Nonrot, &rdev->flags);
		WRITE_ONCE(conf->nonrot_disks, conf->nonrot_disks + 1);
	}

	rdev->raid_disk = disk;
	info->head_position = 0;
	info->seq_start = MaxSector;
	WRITE_ONCE(info->rdev, rdev);

	return true;
}

static bool raid1_remove_conf(struct r1conf *conf, int disk)
{
	struct raid1_info *info = conf->mirrors + disk;
	struct md_rdev *rdev = info->rdev;

	if (!rdev || test_bit(In_sync, &rdev->flags) ||
	    atomic_read(&rdev->nr_pending))
		return false;

	/* Only remove non-faulty devices if recovery is not possible. */
	if (!test_bit(Faulty, &rdev->flags) &&
	    rdev->mddev->recovery_disabled != conf->recovery_disabled &&
	    rdev->mddev->degraded < conf->raid_disks)
		return false;

	if (test_and_clear_bit(Nonrot, &rdev->flags))
		WRITE_ONCE(conf->nonrot_disks, conf->nonrot_disks - 1);

	WRITE_ONCE(info->rdev, NULL);
	return true;
}

static int raid1_add_disk(struct mddev *mddev, struct md_rdev *rdev)
{
	struct r1conf *conf = mddev->private;
	int err = -EEXIST;
	int mirror = 0, repl_slot = -1;
	struct raid1_info *p;
	int first = 0;
	int last = conf->raid_disks - 1;

	if (mddev->recovery_disabled == conf->recovery_disabled)
		return -EBUSY;

	if (rdev->raid_disk >= 0)
		first = last = rdev->raid_disk;

	/*
	 * find the disk ... but prefer rdev->saved_raid_disk
	 * if possible.
	 */
	if (rdev->saved_raid_disk >= 0 &&
	    rdev->saved_raid_disk >= first &&
	    rdev->saved_raid_disk < conf->raid_disks &&
	    conf->mirrors[rdev->saved_raid_disk].rdev == NULL)
		first = last = rdev->saved_raid_disk;

	for (mirror = first; mirror <= last; mirror++) {
		p = conf->mirrors + mirror;
		if (!p->rdev) {
			err = mddev_stack_new_rdev(mddev, rdev);
			if (err)
				return err;

			raid1_add_conf(conf, rdev, mirror, false);
			/* As all devices are equivalent, we don't need a full recovery
			 * if this was recently any drive of the array
			 */
			if (rdev->saved_raid_disk < 0)
				conf->fullsync = 1;
			break;
		}
		if (test_bit(WantReplacement, &p->rdev->flags) &&
		    p[conf->raid_disks].rdev == NULL && repl_slot < 0)
			repl_slot = mirror;
	}

	if (err && repl_slot >= 0) {
		/* Add this device as a replacement */
		clear_bit(In_sync, &rdev->flags);
		set_bit(Replacement, &rdev->flags);
		raid1_add_conf(conf, rdev, repl_slot, true);
		err = 0;
		conf->fullsync = 1;
	}

	print_conf(conf);
	return err;
}

static int raid1_remove_disk(struct mddev *mddev, struct md_rdev *rdev)
{
	struct r1conf *conf = mddev->private;
	int err = 0;
	int number = rdev->raid_disk;
	struct raid1_info *p = conf->mirrors + number;

	if (unlikely(number >= conf->raid_disks))
		goto abort;

	if (rdev != p->rdev) {
		number += conf->raid_disks;
		p = conf->mirrors + number;
	}

	print_conf(conf);
	if (rdev == p->rdev) {
		if (!raid1_remove_conf(conf, number)) {
			err = -EBUSY;
			goto abort;
		}

		if (number < conf->raid_disks &&
		    conf->mirrors[conf->raid_disks + number].rdev) {
			/* We just removed a device that is being replaced.
			 * Move down the replacement.  We drain all IO before
			 * doing this to avoid confusion.
			 */
			struct md_rdev *repl =
				conf->mirrors[conf->raid_disks + number].rdev;
			freeze_array(conf, 0);
			if (atomic_read(&repl->nr_pending)) {
				/* It means that some queued IO of retry_list
				 * hold repl. Thus, we cannot set replacement
				 * as NULL, avoiding rdev NULL pointer
				 * dereference in sync_request_write and
				 * handle_write_finished.
				 */
				err = -EBUSY;
				unfreeze_array(conf);
				goto abort;
			}
			clear_bit(Replacement, &repl->flags);
			WRITE_ONCE(p->rdev, repl);
			conf->mirrors[conf->raid_disks + number].rdev = NULL;
			unfreeze_array(conf);
		}

		clear_bit(WantReplacement, &rdev->flags);
		err = md_integrity_register(mddev);
	}
abort:

	print_conf(conf);
	return err;
}

static void end_sync_read(struct bio *bio)
{
	struct r1bio *r1_bio = get_resync_r1bio(bio);

	update_head_pos(r1_bio->read_disk, r1_bio);

	/*
	 * we have read a block, now it needs to be re-written,
	 * or re-read if the read failed.
	 * We don't do much here, just schedule handling by raid1d
	 */
	if (!bio->bi_status)
		set_bit(R1BIO_Uptodate, &r1_bio->state);

	if (atomic_dec_and_test(&r1_bio->remaining))
		reschedule_retry(r1_bio);
}

static void abort_sync_write(struct mddev *mddev, struct r1bio *r1_bio)
{
	sector_t sync_blocks = 0;
	sector_t s = r1_bio->sector;
	long sectors_to_go = r1_bio->sectors;

	/* make sure these bits don't get cleared. */
	do {
		mddev->bitmap_ops->end_sync(mddev, s, &sync_blocks);
		s += sync_blocks;
		sectors_to_go -= sync_blocks;
	} while (sectors_to_go > 0);
}

static void put_sync_write_buf(struct r1bio *r1_bio, int uptodate)
{
	if (atomic_dec_and_test(&r1_bio->remaining)) {
		struct mddev *mddev = r1_bio->mddev;
		int s = r1_bio->sectors;

		if (test_bit(R1BIO_MadeGood, &r1_bio->state) ||
		    test_bit(R1BIO_WriteError, &r1_bio->state))
			reschedule_retry(r1_bio);
		else {
			put_buf(r1_bio);
			md_done_sync(mddev, s, uptodate);
		}
	}
}

static void end_sync_write(struct bio *bio)
{
	int uptodate = !bio->bi_status;
	struct r1bio *r1_bio = get_resync_r1bio(bio);
	struct mddev *mddev = r1_bio->mddev;
	struct r1conf *conf = mddev->private;
	struct md_rdev *rdev = conf->mirrors[find_bio_disk(r1_bio, bio)].rdev;

	if (!uptodate) {
		abort_sync_write(mddev, r1_bio);
		set_bit(WriteErrorSeen, &rdev->flags);
		if (!test_and_set_bit(WantReplacement, &rdev->flags))
			set_bit(MD_RECOVERY_NEEDED, &
				mddev->recovery);
		set_bit(R1BIO_WriteError, &r1_bio->state);
	} else if (rdev_has_badblock(rdev, r1_bio->sector, r1_bio->sectors) &&
		   !rdev_has_badblock(conf->mirrors[r1_bio->read_disk].rdev,
				      r1_bio->sector, r1_bio->sectors)) {
		set_bit(R1BIO_MadeGood, &r1_bio->state);
	}

	put_sync_write_buf(r1_bio, uptodate);
}

static int r1_sync_page_io(struct md_rdev *rdev, sector_t sector,
			   int sectors, struct page *page, blk_opf_t rw)
{
	if (sync_page_io(rdev, sector, sectors << 9, page, rw, false))
		/* success */
		return 1;
	if (rw == REQ_OP_WRITE) {
		set_bit(WriteErrorSeen, &rdev->flags);
		if (!test_and_set_bit(WantReplacement,
				      &rdev->flags))
			set_bit(MD_RECOVERY_NEEDED, &
				rdev->mddev->recovery);
	}
	/* need to record an error - either for the block or the device */
	if (!rdev_set_badblocks(rdev, sector, sectors, 0))
		md_error(rdev->mddev, rdev);
	return 0;
}

static int fix_sync_read_error(struct r1bio *r1_bio)
{
	/* Try some synchronous reads of other devices to get
	 * good data, much like with normal read errors.  Only
	 * read into the pages we already have so we don't
	 * need to re-issue the read request.
	 * We don't need to freeze the array, because being in an
	 * active sync request, there is no normal IO, and
	 * no overlapping syncs.
	 * We don't need to check is_badblock() again as we
	 * made sure that anything with a bad block in range
	 * will have bi_end_io clear.
	 */
	struct mddev *mddev = r1_bio->mddev;
	struct r1conf *conf = mddev->private;
	struct bio *bio = r1_bio->bios[r1_bio->read_disk];
	struct page **pages = get_resync_pages(bio)->pages;
	sector_t sect = r1_bio->sector;
	int sectors = r1_bio->sectors;
	int idx = 0;
	struct md_rdev *rdev;

	rdev = conf->mirrors[r1_bio->read_disk].rdev;
	if (test_bit(FailFast, &rdev->flags)) {
		/* Don't try recovering from here - just fail it
		 * ... unless it is the last working device of course */
		md_error(mddev, rdev);
		if (test_bit(Faulty, &rdev->flags))
			/* Don't try to read from here, but make sure
			 * put_buf does it's thing
			 */
			bio->bi_end_io = end_sync_write;
	}

	while(sectors) {
		int s = sectors;
		int d = r1_bio->read_disk;
		int success = 0;
		int start;

		if (s > (PAGE_SIZE>>9))
			s = PAGE_SIZE >> 9;
		do {
			if (r1_bio->bios[d]->bi_end_io == end_sync_read) {
				/* No rcu protection needed here devices
				 * can only be removed when no resync is
				 * active, and resync is currently active
				 */
				rdev = conf->mirrors[d].rdev;
				if (sync_page_io(rdev, sect, s<<9,
						 pages[idx],
						 REQ_OP_READ, false)) {
					success = 1;
					break;
				}
			}
			d++;
			if (d == conf->raid_disks * 2)
				d = 0;
		} while (!success && d != r1_bio->read_disk);

		if (!success) {
			int abort = 0;
			/* Cannot read from anywhere, this block is lost.
			 * Record a bad block on each device.  If that doesn't
			 * work just disable and interrupt the recovery.
			 * Don't fail devices as that won't really help.
			 */
			pr_crit_ratelimited("md/raid1:%s: %pg: unrecoverable I/O read error for block %llu\n",
					    mdname(mddev), bio->bi_bdev,
					    (unsigned long long)r1_bio->sector);
			for (d = 0; d < conf->raid_disks * 2; d++) {
				rdev = conf->mirrors[d].rdev;
				if (!rdev || test_bit(Faulty, &rdev->flags))
					continue;
				if (!rdev_set_badblocks(rdev, sect, s, 0))
					abort = 1;
			}
			if (abort) {
				conf->recovery_disabled =
					mddev->recovery_disabled;
				set_bit(MD_RECOVERY_INTR, &mddev->recovery);
				md_done_sync(mddev, r1_bio->sectors, 0);
				put_buf(r1_bio);
				return 0;
			}
			/* Try next page */
			sectors -= s;
			sect += s;
			idx++;
			continue;
		}

		start = d;
		/* write it back and re-read */
		while (d != r1_bio->read_disk) {
			if (d == 0)
				d = conf->raid_disks * 2;
			d--;
			if (r1_bio->bios[d]->bi_end_io != end_sync_read)
				continue;
			rdev = conf->mirrors[d].rdev;
			if (r1_sync_page_io(rdev, sect, s,
					    pages[idx],
					    REQ_OP_WRITE) == 0) {
				r1_bio->bios[d]->bi_end_io = NULL;
				rdev_dec_pending(rdev, mddev);
			}
		}
		d = start;
		while (d != r1_bio->read_disk) {
			if (d == 0)
				d = conf->raid_disks * 2;
			d--;
			if (r1_bio->bios[d]->bi_end_io != end_sync_read)
				continue;
			rdev = conf->mirrors[d].rdev;
			if (r1_sync_page_io(rdev, sect, s,
					    pages[idx],
					    REQ_OP_READ) != 0)
				atomic_add(s, &rdev->corrected_errors);
		}
		sectors -= s;
		sect += s;
		idx ++;
	}
	set_bit(R1BIO_Uptodate, &r1_bio->state);
	bio->bi_status = 0;
	return 1;
}

static void process_checks(struct r1bio *r1_bio)
{
	/* We have read all readable devices.  If we haven't
	 * got the block, then there is no hope left.
	 * If we have, then we want to do a comparison
	 * and skip the write if everything is the same.
	 * If any blocks failed to read, then we need to
	 * attempt an over-write
	 */
	struct mddev *mddev = r1_bio->mddev;
	struct r1conf *conf = mddev->private;
	int primary;
	int i;
	int vcnt;

	/* Fix variable parts of all bios */
	vcnt = (r1_bio->sectors + PAGE_SIZE / 512 - 1) >> (PAGE_SHIFT - 9);
	for (i = 0; i < conf->raid_disks * 2; i++) {
		blk_status_t status;
		struct bio *b = r1_bio->bios[i];
		struct resync_pages *rp = get_resync_pages(b);
		if (b->bi_end_io != end_sync_read)
			continue;
		/* fixup the bio for reuse, but preserve errno */
		status = b->bi_status;
		bio_reset(b, conf->mirrors[i].rdev->bdev, REQ_OP_READ);
		b->bi_status = status;
		b->bi_iter.bi_sector = r1_bio->sector +
			conf->mirrors[i].rdev->data_offset;
		b->bi_end_io = end_sync_read;
		rp->raid_bio = r1_bio;
		b->bi_private = rp;

		/* initialize bvec table again */
		md_bio_reset_resync_pages(b, rp, r1_bio->sectors << 9);
	}
	for (primary = 0; primary < conf->raid_disks * 2; primary++)
		if (r1_bio->bios[primary]->bi_end_io == end_sync_read &&
		    !r1_bio->bios[primary]->bi_status) {
			r1_bio->bios[primary]->bi_end_io = NULL;
			rdev_dec_pending(conf->mirrors[primary].rdev, mddev);
			break;
		}
	r1_bio->read_disk = primary;
	for (i = 0; i < conf->raid_disks * 2; i++) {
		int j = 0;
		struct bio *pbio = r1_bio->bios[primary];
		struct bio *sbio = r1_bio->bios[i];
		blk_status_t status = sbio->bi_status;
		struct page **ppages = get_resync_pages(pbio)->pages;
		struct page **spages = get_resync_pages(sbio)->pages;
		struct bio_vec *bi;
		int page_len[RESYNC_PAGES] = { 0 };
		struct bvec_iter_all iter_all;

		if (sbio->bi_end_io != end_sync_read)
			continue;
		/* Now we can 'fixup' the error value */
		sbio->bi_status = 0;

		bio_for_each_segment_all(bi, sbio, iter_all)
			page_len[j++] = bi->bv_len;

		if (!status) {
			for (j = vcnt; j-- ; ) {
				if (memcmp(page_address(ppages[j]),
					   page_address(spages[j]),
					   page_len[j]))
					break;
			}
		} else
			j = 0;
		if (j >= 0)
			atomic64_add(r1_bio->sectors, &mddev->resync_mismatches);
		if (j < 0 || (test_bit(MD_RECOVERY_CHECK, &mddev->recovery)
			      && !status)) {
			/* No need to write to this device. */
			sbio->bi_end_io = NULL;
			rdev_dec_pending(conf->mirrors[i].rdev, mddev);
			continue;
		}

		bio_copy_data(sbio, pbio);
	}
}

static void sync_request_write(struct mddev *mddev, struct r1bio *r1_bio)
{
	struct r1conf *conf = mddev->private;
	int i;
	int disks = conf->raid_disks * 2;
	struct bio *wbio;

	if (!test_bit(R1BIO_Uptodate, &r1_bio->state))
		/* ouch - failed to read all of that. */
		if (!fix_sync_read_error(r1_bio))
			return;

	if (test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery))
		process_checks(r1_bio);

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
		if (test_bit(Faulty, &conf->mirrors[i].rdev->flags)) {
			abort_sync_write(mddev, r1_bio);
			continue;
		}

		wbio->bi_opf = REQ_OP_WRITE;
		if (test_bit(FailFast, &conf->mirrors[i].rdev->flags))
			wbio->bi_opf |= MD_FAILFAST;

		wbio->bi_end_io = end_sync_write;
		atomic_inc(&r1_bio->remaining);
		md_sync_acct(conf->mirrors[i].rdev->bdev, bio_sectors(wbio));

		submit_bio_noacct(wbio);
	}

	put_sync_write_buf(r1_bio, 1);
}

/*
 * This is a kernel thread which:
 *
 *	1.	Retries failed read operations on working mirrors.
 *	2.	Updates the raid superblock when problems encounter.
 *	3.	Performs writes following reads for array synchronising.
 */

static void fix_read_error(struct r1conf *conf, struct r1bio *r1_bio)
{
	sector_t sect = r1_bio->sector;
	int sectors = r1_bio->sectors;
	int read_disk = r1_bio->read_disk;
	struct mddev *mddev = conf->mddev;
	struct md_rdev *rdev = conf->mirrors[read_disk].rdev;

	if (exceed_read_errors(mddev, rdev)) {
		r1_bio->bios[r1_bio->read_disk] = IO_BLOCKED;
		return;
	}

	while(sectors) {
		int s = sectors;
		int d = read_disk;
		int success = 0;
		int start;

		if (s > (PAGE_SIZE>>9))
			s = PAGE_SIZE >> 9;

		do {
			rdev = conf->mirrors[d].rdev;
			if (rdev &&
			    (test_bit(In_sync, &rdev->flags) ||
			     (!test_bit(Faulty, &rdev->flags) &&
			      rdev->recovery_offset >= sect + s)) &&
			    rdev_has_badblock(rdev, sect, s) == 0) {
				atomic_inc(&rdev->nr_pending);
				if (sync_page_io(rdev, sect, s<<9,
					 conf->tmppage, REQ_OP_READ, false))
					success = 1;
				rdev_dec_pending(rdev, mddev);
				if (success)
					break;
			}

			d++;
			if (d == conf->raid_disks * 2)
				d = 0;
		} while (d != read_disk);

		if (!success) {
			/* Cannot read from anywhere - mark it bad */
			struct md_rdev *rdev = conf->mirrors[read_disk].rdev;
			if (!rdev_set_badblocks(rdev, sect, s, 0))
				md_error(mddev, rdev);
			break;
		}
		/* write it back and re-read */
		start = d;
		while (d != read_disk) {
			if (d==0)
				d = conf->raid_disks * 2;
			d--;
			rdev = conf->mirrors[d].rdev;
			if (rdev &&
			    !test_bit(Faulty, &rdev->flags)) {
				atomic_inc(&rdev->nr_pending);
				r1_sync_page_io(rdev, sect, s,
						conf->tmppage, REQ_OP_WRITE);
				rdev_dec_pending(rdev, mddev);
			}
		}
		d = start;
		while (d != read_disk) {
			if (d==0)
				d = conf->raid_disks * 2;
			d--;
			rdev = conf->mirrors[d].rdev;
			if (rdev &&
			    !test_bit(Faulty, &rdev->flags)) {
				atomic_inc(&rdev->nr_pending);
				if (r1_sync_page_io(rdev, sect, s,
						conf->tmppage, REQ_OP_READ)) {
					atomic_add(s, &rdev->corrected_errors);
					pr_info("md/raid1:%s: read error corrected (%d sectors at %llu on %pg)\n",
						mdname(mddev), s,
						(unsigned long long)(sect +
								     rdev->data_offset),
						rdev->bdev);
				}
				rdev_dec_pending(rdev, mddev);
			}
		}
		sectors -= s;
		sect += s;
	}
}

static int narrow_write_error(struct r1bio *r1_bio, int i)
{
	struct mddev *mddev = r1_bio->mddev;
	struct r1conf *conf = mddev->private;
	struct md_rdev *rdev = conf->mirrors[i].rdev;

	/* bio has the data to be written to device 'i' where
	 * we just recently had a write error.
	 * We repeatedly clone the bio and trim down to one block,
	 * then try the write.  Where the write fails we record
	 * a bad block.
	 * It is conceivable that the bio doesn't exactly align with
	 * blocks.  We must handle this somehow.
	 *
	 * We currently own a reference on the rdev.
	 */

	int block_sectors;
	sector_t sector;
	int sectors;
	int sect_to_write = r1_bio->sectors;
	int ok = 1;

	if (rdev->badblocks.shift < 0)
		return 0;

	block_sectors = roundup(1 << rdev->badblocks.shift,
				bdev_logical_block_size(rdev->bdev) >> 9);
	sector = r1_bio->sector;
	sectors = ((sector + block_sectors)
		   & ~(sector_t)(block_sectors - 1))
		- sector;

	while (sect_to_write) {
		struct bio *wbio;
		if (sectors > sect_to_write)
			sectors = sect_to_write;
		/* Write at 'sector' for 'sectors'*/

		if (test_bit(R1BIO_BehindIO, &r1_bio->state)) {
			wbio = bio_alloc_clone(rdev->bdev,
					       r1_bio->behind_master_bio,
					       GFP_NOIO, &mddev->bio_set);
		} else {
			wbio = bio_alloc_clone(rdev->bdev, r1_bio->master_bio,
					       GFP_NOIO, &mddev->bio_set);
		}

		wbio->bi_opf = REQ_OP_WRITE;
		wbio->bi_iter.bi_sector = r1_bio->sector;
		wbio->bi_iter.bi_size = r1_bio->sectors << 9;

		bio_trim(wbio, sector - r1_bio->sector, sectors);
		wbio->bi_iter.bi_sector += rdev->data_offset;

		if (submit_bio_wait(wbio) < 0)
			/* failure! */
			ok = rdev_set_badblocks(rdev, sector,
						sectors, 0)
				&& ok;

		bio_put(wbio);
		sect_to_write -= sectors;
		sector += sectors;
		sectors = block_sectors;
	}
	return ok;
}

static void handle_sync_write_finished(struct r1conf *conf, struct r1bio *r1_bio)
{
	int m;
	int s = r1_bio->sectors;
	for (m = 0; m < conf->raid_disks * 2 ; m++) {
		struct md_rdev *rdev = conf->mirrors[m].rdev;
		struct bio *bio = r1_bio->bios[m];
		if (bio->bi_end_io == NULL)
			continue;
		if (!bio->bi_status &&
		    test_bit(R1BIO_MadeGood, &r1_bio->state)) {
			rdev_clear_badblocks(rdev, r1_bio->sector, s, 0);
		}
		if (bio->bi_status &&
		    test_bit(R1BIO_WriteError, &r1_bio->state)) {
			if (!rdev_set_badblocks(rdev, r1_bio->sector, s, 0))
				md_error(conf->mddev, rdev);
		}
	}
	put_buf(r1_bio);
	md_done_sync(conf->mddev, s, 1);
}

static void handle_write_finished(struct r1conf *conf, struct r1bio *r1_bio)
{
	int m, idx;
	bool fail = false;

	for (m = 0; m < conf->raid_disks * 2 ; m++)
		if (r1_bio->bios[m] == IO_MADE_GOOD) {
			struct md_rdev *rdev = conf->mirrors[m].rdev;
			rdev_clear_badblocks(rdev,
					     r1_bio->sector,
					     r1_bio->sectors, 0);
			rdev_dec_pending(rdev, conf->mddev);
		} else if (r1_bio->bios[m] != NULL) {
			/* This drive got a write error.  We need to
			 * narrow down and record precise write
			 * errors.
			 */
			fail = true;
			if (!narrow_write_error(r1_bio, m))
				md_error(conf->mddev,
					 conf->mirrors[m].rdev);
				/* an I/O failed, we can't clear the bitmap */
			rdev_dec_pending(conf->mirrors[m].rdev,
					 conf->mddev);
		}
	if (fail) {
		spin_lock_irq(&conf->device_lock);
		list_add(&r1_bio->retry_list, &conf->bio_end_io_list);
		idx = sector_to_idx(r1_bio->sector);
		atomic_inc(&conf->nr_queued[idx]);
		spin_unlock_irq(&conf->device_lock);
		/*
		 * In case freeze_array() is waiting for condition
		 * get_unqueued_pending() == extra to be true.
		 */
		wake_up(&conf->wait_barrier);
		md_wakeup_thread(conf->mddev->thread);
	} else {
		if (test_bit(R1BIO_WriteError, &r1_bio->state))
			close_write(r1_bio);
		raid_end_bio_io(r1_bio);
	}
}

static void handle_read_error(struct r1conf *conf, struct r1bio *r1_bio)
{
	struct mddev *mddev = conf->mddev;
	struct bio *bio;
	struct md_rdev *rdev;
	sector_t sector;

	clear_bit(R1BIO_ReadError, &r1_bio->state);
	/* we got a read error. Maybe the drive is bad.  Maybe just
	 * the block and we can fix it.
	 * We freeze all other IO, and try reading the block from
	 * other devices.  When we find one, we re-write
	 * and check it that fixes the read error.
	 * This is all done synchronously while the array is
	 * frozen
	 */

	bio = r1_bio->bios[r1_bio->read_disk];
	bio_put(bio);
	r1_bio->bios[r1_bio->read_disk] = NULL;

	rdev = conf->mirrors[r1_bio->read_disk].rdev;
	if (mddev->ro == 0
	    && !test_bit(FailFast, &rdev->flags)) {
		freeze_array(conf, 1);
		fix_read_error(conf, r1_bio);
		unfreeze_array(conf);
	} else if (mddev->ro == 0 && test_bit(FailFast, &rdev->flags)) {
		md_error(mddev, rdev);
	} else {
		r1_bio->bios[r1_bio->read_disk] = IO_BLOCKED;
	}

	rdev_dec_pending(rdev, conf->mddev);
	sector = r1_bio->sector;
	bio = r1_bio->master_bio;

	/* Reuse the old r1_bio so that the IO_BLOCKED settings are preserved */
	r1_bio->state = 0;
	raid1_read_request(mddev, bio, r1_bio->sectors, r1_bio);
	allow_barrier(conf, sector);
}

static void raid1d(struct md_thread *thread)
{
	struct mddev *mddev = thread->mddev;
	struct r1bio *r1_bio;
	unsigned long flags;
	struct r1conf *conf = mddev->private;
	struct list_head *head = &conf->retry_list;
	struct blk_plug plug;
	int idx;

	md_check_recovery(mddev);

	if (!list_empty_careful(&conf->bio_end_io_list) &&
	    !test_bit(MD_SB_CHANGE_PENDING, &mddev->sb_flags)) {
		LIST_HEAD(tmp);
		spin_lock_irqsave(&conf->device_lock, flags);
		if (!test_bit(MD_SB_CHANGE_PENDING, &mddev->sb_flags))
			list_splice_init(&conf->bio_end_io_list, &tmp);
		spin_unlock_irqrestore(&conf->device_lock, flags);
		while (!list_empty(&tmp)) {
			r1_bio = list_first_entry(&tmp, struct r1bio,
						  retry_list);
			list_del(&r1_bio->retry_list);
			idx = sector_to_idx(r1_bio->sector);
			atomic_dec(&conf->nr_queued[idx]);
			if (test_bit(R1BIO_WriteError, &r1_bio->state))
				close_write(r1_bio);
			raid_end_bio_io(r1_bio);
		}
	}

	blk_start_plug(&plug);
	for (;;) {

		flush_pending_writes(conf);

		spin_lock_irqsave(&conf->device_lock, flags);
		if (list_empty(head)) {
			spin_unlock_irqrestore(&conf->device_lock, flags);
			break;
		}
		r1_bio = list_entry(head->prev, struct r1bio, retry_list);
		list_del(head->prev);
		idx = sector_to_idx(r1_bio->sector);
		atomic_dec(&conf->nr_queued[idx]);
		spin_unlock_irqrestore(&conf->device_lock, flags);

		mddev = r1_bio->mddev;
		conf = mddev->private;
		if (test_bit(R1BIO_IsSync, &r1_bio->state)) {
			if (test_bit(R1BIO_MadeGood, &r1_bio->state) ||
			    test_bit(R1BIO_WriteError, &r1_bio->state))
				handle_sync_write_finished(conf, r1_bio);
			else
				sync_request_write(mddev, r1_bio);
		} else if (test_bit(R1BIO_MadeGood, &r1_bio->state) ||
			   test_bit(R1BIO_WriteError, &r1_bio->state))
			handle_write_finished(conf, r1_bio);
		else if (test_bit(R1BIO_ReadError, &r1_bio->state))
			handle_read_error(conf, r1_bio);
		else
			WARN_ON_ONCE(1);

		cond_resched();
		if (mddev->sb_flags & ~(1<<MD_SB_CHANGE_PENDING))
			md_check_recovery(mddev);
	}
	blk_finish_plug(&plug);
}

static int init_resync(struct r1conf *conf)
{
	int buffs;

	buffs = RESYNC_WINDOW / RESYNC_BLOCK_SIZE;
	BUG_ON(mempool_initialized(&conf->r1buf_pool));

	return mempool_init(&conf->r1buf_pool, buffs, r1buf_pool_alloc,
			    r1buf_pool_free, conf->poolinfo);
}

static struct r1bio *raid1_alloc_init_r1buf(struct r1conf *conf)
{
	struct r1bio *r1bio = mempool_alloc(&conf->r1buf_pool, GFP_NOIO);
	struct resync_pages *rps;
	struct bio *bio;
	int i;

	for (i = conf->poolinfo->raid_disks; i--; ) {
		bio = r1bio->bios[i];
		rps = bio->bi_private;
		bio_reset(bio, NULL, 0);
		bio->bi_private = rps;
	}
	r1bio->master_bio = NULL;
	return r1bio;
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

static sector_t raid1_sync_request(struct mddev *mddev, sector_t sector_nr,
				   sector_t max_sector, int *skipped)
{
	struct r1conf *conf = mddev->private;
	struct r1bio *r1_bio;
	struct bio *bio;
	sector_t nr_sectors;
	int disk = -1;
	int i;
	int wonly = -1;
	int write_targets = 0, read_targets = 0;
	sector_t sync_blocks;
	bool still_degraded = false;
	int good_sectors = RESYNC_SECTORS;
	int min_bad = 0; /* number of sectors that are bad in all devices */
	int idx = sector_to_idx(sector_nr);
	int page_idx = 0;

	if (!mempool_initialized(&conf->r1buf_pool))
		if (init_resync(conf))
			return 0;

	if (sector_nr >= max_sector) {
		/* If we aborted, we need to abort the
		 * sync on the 'current' bitmap chunk (there will
		 * only be one in raid1 resync.
		 * We can find the current addess in mddev->curr_resync
		 */
		if (mddev->curr_resync < max_sector) /* aborted */
			mddev->bitmap_ops->end_sync(mddev, mddev->curr_resync,
						    &sync_blocks);
		else /* completed sync */
			conf->fullsync = 0;

		mddev->bitmap_ops->close_sync(mddev);
		close_sync(conf);

		if (mddev_is_clustered(mddev)) {
			conf->cluster_sync_low = 0;
			conf->cluster_sync_high = 0;
		}
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
	if (!mddev->bitmap_ops->start_sync(mddev, sector_nr, &sync_blocks, true) &&
	    !conf->fullsync && !test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery)) {
		/* We can skip this block, and probably several more */
		*skipped = 1;
		return sync_blocks;
	}

	/*
	 * If there is non-resync activity waiting for a turn, then let it
	 * though before starting on this new sync request.
	 */
	if (atomic_read(&conf->nr_waiting[idx]))
		schedule_timeout_uninterruptible(1);

	/* we are incrementing sector_nr below. To be safe, we check against
	 * sector_nr + two times RESYNC_SECTORS
	 */

	mddev->bitmap_ops->cond_end_sync(mddev, sector_nr,
		mddev_is_clustered(mddev) &&
		(sector_nr + 2 * RESYNC_SECTORS > conf->cluster_sync_high));

	if (raise_barrier(conf, sector_nr))
		return 0;

	r1_bio = raid1_alloc_init_r1buf(conf);

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
	/* make sure good_sectors won't go across barrier unit boundary */
	good_sectors = align_to_barrier_unit_end(sector_nr, good_sectors);

	for (i = 0; i < conf->raid_disks * 2; i++) {
		struct md_rdev *rdev;
		bio = r1_bio->bios[i];

		rdev = conf->mirrors[i].rdev;
		if (rdev == NULL ||
		    test_bit(Faulty, &rdev->flags)) {
			if (i < conf->raid_disks)
				still_degraded = true;
		} else if (!test_bit(In_sync, &rdev->flags)) {
			bio->bi_opf = REQ_OP_WRITE;
			bio->bi_end_io = end_sync_write;
			write_targets ++;
		} else {
			/* may need to read from here */
			sector_t first_bad = MaxSector;
			int bad_sectors;

			if (is_badblock(rdev, sector_nr, good_sectors,
					&first_bad, &bad_sectors)) {
				if (first_bad > sector_nr)
					good_sectors = first_bad - sector_nr;
				else {
					bad_sectors -= (sector_nr - first_bad);
					if (min_bad == 0 ||
					    min_bad > bad_sectors)
						min_bad = bad_sectors;
				}
			}
			if (sector_nr < first_bad) {
				if (test_bit(WriteMostly, &rdev->flags)) {
					if (wonly < 0)
						wonly = i;
				} else {
					if (disk < 0)
						disk = i;
				}
				bio->bi_opf = REQ_OP_READ;
				bio->bi_end_io = end_sync_read;
				read_targets++;
			} else if (!test_bit(WriteErrorSeen, &rdev->flags) &&
				test_bit(MD_RECOVERY_SYNC, &mddev->recovery) &&
				!test_bit(MD_RECOVERY_CHECK, &mddev->recovery)) {
				/*
				 * The device is suitable for reading (InSync),
				 * but has bad block(s) here. Let's try to correct them,
				 * if we are doing resync or repair. Otherwise, leave
				 * this device alone for this sync request.
				 */
				bio->bi_opf = REQ_OP_WRITE;
				bio->bi_end_io = end_sync_write;
				write_targets++;
			}
		}
		if (rdev && bio->bi_end_io) {
			atomic_inc(&rdev->nr_pending);
			bio->bi_iter.bi_sector = sector_nr + rdev->data_offset;
			bio_set_dev(bio, rdev->bdev);
			if (test_bit(FailFast, &rdev->flags))
				bio->bi_opf |= MD_FAILFAST;
		}
	}
	if (disk < 0)
		disk = wonly;
	r1_bio->read_disk = disk;

	if (read_targets == 0 && min_bad > 0) {
		/* These sectors are bad on all InSync devices, so we
		 * need to mark them bad on all write targets
		 */
		int ok = 1;
		for (i = 0 ; i < conf->raid_disks * 2 ; i++)
			if (r1_bio->bios[i]->bi_end_io == end_sync_write) {
				struct md_rdev *rdev = conf->mirrors[i].rdev;
				ok = rdev_set_badblocks(rdev, sector_nr,
							min_bad, 0
					) && ok;
			}
		set_bit(MD_SB_CHANGE_DEVS, &mddev->sb_flags);
		*skipped = 1;
		put_buf(r1_bio);

		if (!ok) {
			/* Cannot record the badblocks, so need to
			 * abort the resync.
			 * If there are multiple read targets, could just
			 * fail the really bad ones ???
			 */
			conf->recovery_disabled = mddev->recovery_disabled;
			set_bit(MD_RECOVERY_INTR, &mddev->recovery);
			return 0;
		} else
			return min_bad;

	}
	if (min_bad > 0 && min_bad < good_sectors) {
		/* only resync enough to reach the next bad->good
		 * transition */
		good_sectors = min_bad;
	}

	if (test_bit(MD_RECOVERY_SYNC, &mddev->recovery) && read_targets > 0)
		/* extra read targets are also write targets */
		write_targets += read_targets-1;

	if (write_targets == 0 || read_targets == 0) {
		/* There is nowhere to write, so all non-sync
		 * drives must be failed - so we are finished
		 */
		sector_t rv;
		if (min_bad > 0)
			max_sector = sector_nr + min_bad;
		rv = max_sector - sector_nr;
		*skipped = 1;
		put_buf(r1_bio);
		return rv;
	}

	if (max_sector > mddev->resync_max)
		max_sector = mddev->resync_max; /* Don't do IO beyond here */
	if (max_sector > sector_nr + good_sectors)
		max_sector = sector_nr + good_sectors;
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
			if (!mddev->bitmap_ops->start_sync(mddev, sector_nr,
						&sync_blocks, still_degraded) &&
			    !conf->fullsync &&
			    !test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery))
				break;
			if ((len >> 9) > sync_blocks)
				len = sync_blocks<<9;
		}

		for (i = 0 ; i < conf->raid_disks * 2; i++) {
			struct resync_pages *rp;

			bio = r1_bio->bios[i];
			rp = get_resync_pages(bio);
			if (bio->bi_end_io) {
				page = resync_fetch_page(rp, page_idx);

				/*
				 * won't fail because the vec table is big
				 * enough to hold all these pages
				 */
				__bio_add_page(bio, page, len, 0);
			}
		}
		nr_sectors += len>>9;
		sector_nr += len>>9;
		sync_blocks -= (len>>9);
	} while (++page_idx < RESYNC_PAGES);

	r1_bio->sectors = nr_sectors;

	if (mddev_is_clustered(mddev) &&
			conf->cluster_sync_high < sector_nr + nr_sectors) {
		conf->cluster_sync_low = mddev->curr_resync_completed;
		conf->cluster_sync_high = conf->cluster_sync_low + CLUSTER_RESYNC_WINDOW_SECTORS;
		/* Send resync message */
		md_cluster_ops->resync_info_update(mddev,
				conf->cluster_sync_low,
				conf->cluster_sync_high);
	}

	/* For a user-requested sync, we read all readable devices and do a
	 * compare
	 */
	if (test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery)) {
		atomic_set(&r1_bio->remaining, read_targets);
		for (i = 0; i < conf->raid_disks * 2 && read_targets; i++) {
			bio = r1_bio->bios[i];
			if (bio->bi_end_io == end_sync_read) {
				read_targets--;
				md_sync_acct_bio(bio, nr_sectors);
				if (read_targets == 1)
					bio->bi_opf &= ~MD_FAILFAST;
				submit_bio_noacct(bio);
			}
		}
	} else {
		atomic_set(&r1_bio->remaining, 1);
		bio = r1_bio->bios[r1_bio->read_disk];
		md_sync_acct_bio(bio, nr_sectors);
		if (read_targets == 1)
			bio->bi_opf &= ~MD_FAILFAST;
		submit_bio_noacct(bio);
	}
	return nr_sectors;
}

static sector_t raid1_size(struct mddev *mddev, sector_t sectors, int raid_disks)
{
	if (sectors)
		return sectors;

	return mddev->dev_sectors;
}

static struct r1conf *setup_conf(struct mddev *mddev)
{
	struct r1conf *conf;
	int i;
	struct raid1_info *disk;
	struct md_rdev *rdev;
	int err = -ENOMEM;

	conf = kzalloc(sizeof(struct r1conf), GFP_KERNEL);
	if (!conf)
		goto abort;

	conf->nr_pending = kcalloc(BARRIER_BUCKETS_NR,
				   sizeof(atomic_t), GFP_KERNEL);
	if (!conf->nr_pending)
		goto abort;

	conf->nr_waiting = kcalloc(BARRIER_BUCKETS_NR,
				   sizeof(atomic_t), GFP_KERNEL);
	if (!conf->nr_waiting)
		goto abort;

	conf->nr_queued = kcalloc(BARRIER_BUCKETS_NR,
				  sizeof(atomic_t), GFP_KERNEL);
	if (!conf->nr_queued)
		goto abort;

	conf->barrier = kcalloc(BARRIER_BUCKETS_NR,
				sizeof(atomic_t), GFP_KERNEL);
	if (!conf->barrier)
		goto abort;

	conf->mirrors = kzalloc(array3_size(sizeof(struct raid1_info),
					    mddev->raid_disks, 2),
				GFP_KERNEL);
	if (!conf->mirrors)
		goto abort;

	conf->tmppage = alloc_page(GFP_KERNEL);
	if (!conf->tmppage)
		goto abort;

	conf->poolinfo = kzalloc(sizeof(*conf->poolinfo), GFP_KERNEL);
	if (!conf->poolinfo)
		goto abort;
	conf->poolinfo->raid_disks = mddev->raid_disks * 2;
	err = mempool_init(&conf->r1bio_pool, NR_RAID_BIOS, r1bio_pool_alloc,
			   rbio_pool_free, conf->poolinfo);
	if (err)
		goto abort;

	err = bioset_init(&conf->bio_split, BIO_POOL_SIZE, 0, 0);
	if (err)
		goto abort;

	conf->poolinfo->mddev = mddev;

	err = -EINVAL;
	spin_lock_init(&conf->device_lock);
	conf->raid_disks = mddev->raid_disks;
	rdev_for_each(rdev, mddev) {
		int disk_idx = rdev->raid_disk;

		if (disk_idx >= conf->raid_disks || disk_idx < 0)
			continue;

		if (!raid1_add_conf(conf, rdev, disk_idx,
				    test_bit(Replacement, &rdev->flags)))
			goto abort;
	}
	conf->mddev = mddev;
	INIT_LIST_HEAD(&conf->retry_list);
	INIT_LIST_HEAD(&conf->bio_end_io_list);

	spin_lock_init(&conf->resync_lock);
	init_waitqueue_head(&conf->wait_barrier);

	bio_list_init(&conf->pending_bio_list);
	conf->recovery_disabled = mddev->recovery_disabled - 1;

	err = -EIO;
	for (i = 0; i < conf->raid_disks * 2; i++) {

		disk = conf->mirrors + i;

		if (i < conf->raid_disks &&
		    disk[conf->raid_disks].rdev) {
			/* This slot has a replacement. */
			if (!disk->rdev) {
				/* No original, just make the replacement
				 * a recovering spare
				 */
				disk->rdev =
					disk[conf->raid_disks].rdev;
				disk[conf->raid_disks].rdev = NULL;
			} else if (!test_bit(In_sync, &disk->rdev->flags))
				/* Original is not in_sync - bad */
				goto abort;
		}

		if (!disk->rdev ||
		    !test_bit(In_sync, &disk->rdev->flags)) {
			disk->head_position = 0;
			if (disk->rdev &&
			    (disk->rdev->saved_raid_disk < 0))
				conf->fullsync = 1;
		}
	}

	err = -ENOMEM;
	rcu_assign_pointer(conf->thread,
			   md_register_thread(raid1d, mddev, "raid1"));
	if (!conf->thread)
		goto abort;

	return conf;

 abort:
	if (conf) {
		mempool_exit(&conf->r1bio_pool);
		kfree(conf->mirrors);
		safe_put_page(conf->tmppage);
		kfree(conf->poolinfo);
		kfree(conf->nr_pending);
		kfree(conf->nr_waiting);
		kfree(conf->nr_queued);
		kfree(conf->barrier);
		bioset_exit(&conf->bio_split);
		kfree(conf);
	}
	return ERR_PTR(err);
}

static int raid1_set_limits(struct mddev *mddev)
{
	struct queue_limits lim;
	int err;

	md_init_stacking_limits(&lim);
	lim.max_write_zeroes_sectors = 0;
	lim.features |= BLK_FEAT_ATOMIC_WRITES;
	err = mddev_stack_rdev_limits(mddev, &lim, MDDEV_STACK_INTEGRITY);
	if (err)
		return err;
	return queue_limits_set(mddev->gendisk->queue, &lim);
}

static int raid1_run(struct mddev *mddev)
{
	struct r1conf *conf;
	int i;
	int ret;

	if (mddev->level != 1) {
		pr_warn("md/raid1:%s: raid level not set to mirroring (%d)\n",
			mdname(mddev), mddev->level);
		return -EIO;
	}
	if (mddev->reshape_position != MaxSector) {
		pr_warn("md/raid1:%s: reshape_position set but not supported\n",
			mdname(mddev));
		return -EIO;
	}

	/*
	 * copy the already verified devices into our private RAID1
	 * bookkeeping area. [whatever we allocate in run(),
	 * should be freed in raid1_free()]
	 */
	if (mddev->private == NULL)
		conf = setup_conf(mddev);
	else
		conf = mddev->private;

	if (IS_ERR(conf))
		return PTR_ERR(conf);

	if (!mddev_is_dm(mddev)) {
		ret = raid1_set_limits(mddev);
		if (ret)
			return ret;
	}

	mddev->degraded = 0;
	for (i = 0; i < conf->raid_disks; i++)
		if (conf->mirrors[i].rdev == NULL ||
		    !test_bit(In_sync, &conf->mirrors[i].rdev->flags) ||
		    test_bit(Faulty, &conf->mirrors[i].rdev->flags))
			mddev->degraded++;
	/*
	 * RAID1 needs at least one disk in active
	 */
	if (conf->raid_disks - mddev->degraded < 1) {
		md_unregister_thread(mddev, &conf->thread);
		return -EINVAL;
	}

	if (conf->raid_disks - mddev->degraded == 1)
		mddev->recovery_cp = MaxSector;

	if (mddev->recovery_cp != MaxSector)
		pr_info("md/raid1:%s: not clean -- starting background reconstruction\n",
			mdname(mddev));
	pr_info("md/raid1:%s: active with %d out of %d mirrors\n",
		mdname(mddev), mddev->raid_disks - mddev->degraded,
		mddev->raid_disks);

	/*
	 * Ok, everything is just fine now
	 */
	rcu_assign_pointer(mddev->thread, conf->thread);
	rcu_assign_pointer(conf->thread, NULL);
	mddev->private = conf;
	set_bit(MD_FAILFAST_SUPPORTED, &mddev->flags);

	md_set_array_sectors(mddev, raid1_size(mddev, 0, 0));

	ret = md_integrity_register(mddev);
	if (ret)
		md_unregister_thread(mddev, &mddev->thread);
	return ret;
}

static void raid1_free(struct mddev *mddev, void *priv)
{
	struct r1conf *conf = priv;

	mempool_exit(&conf->r1bio_pool);
	kfree(conf->mirrors);
	safe_put_page(conf->tmppage);
	kfree(conf->poolinfo);
	kfree(conf->nr_pending);
	kfree(conf->nr_waiting);
	kfree(conf->nr_queued);
	kfree(conf->barrier);
	bioset_exit(&conf->bio_split);
	kfree(conf);
}

static int raid1_resize(struct mddev *mddev, sector_t sectors)
{
	/* no resync is happening, and there is enough space
	 * on all devices, so we can resize.
	 * We need to make sure resync covers any new space.
	 * If the array is shrinking we should possibly wait until
	 * any io in the removed space completes, but it hardly seems
	 * worth it.
	 */
	sector_t newsize = raid1_size(mddev, sectors, 0);
	int ret;

	if (mddev->external_size &&
	    mddev->array_sectors > newsize)
		return -EINVAL;

	ret = mddev->bitmap_ops->resize(mddev, newsize, 0, false);
	if (ret)
		return ret;

	md_set_array_sectors(mddev, newsize);
	if (sectors > mddev->dev_sectors &&
	    mddev->recovery_cp > mddev->dev_sectors) {
		mddev->recovery_cp = mddev->dev_sectors;
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	}
	mddev->dev_sectors = sectors;
	mddev->resync_max_sectors = sectors;
	return 0;
}

static int raid1_reshape(struct mddev *mddev)
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
	mempool_t newpool, oldpool;
	struct pool_info *newpoolinfo;
	struct raid1_info *newmirrors;
	struct r1conf *conf = mddev->private;
	int cnt, raid_disks;
	unsigned long flags;
	int d, d2;
	int ret;

	memset(&newpool, 0, sizeof(newpool));
	memset(&oldpool, 0, sizeof(oldpool));

	/* Cannot change chunk_size, layout, or level */
	if (mddev->chunk_sectors != mddev->new_chunk_sectors ||
	    mddev->layout != mddev->new_layout ||
	    mddev->level != mddev->new_level) {
		mddev->new_chunk_sectors = mddev->chunk_sectors;
		mddev->new_layout = mddev->layout;
		mddev->new_level = mddev->level;
		return -EINVAL;
	}

	if (!mddev_is_clustered(mddev))
		md_allow_write(mddev);

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
	newpoolinfo->raid_disks = raid_disks * 2;

	ret = mempool_init(&newpool, NR_RAID_BIOS, r1bio_pool_alloc,
			   rbio_pool_free, newpoolinfo);
	if (ret) {
		kfree(newpoolinfo);
		return ret;
	}
	newmirrors = kzalloc(array3_size(sizeof(struct raid1_info),
					 raid_disks, 2),
			     GFP_KERNEL);
	if (!newmirrors) {
		kfree(newpoolinfo);
		mempool_exit(&newpool);
		return -ENOMEM;
	}

	freeze_array(conf, 0);

	/* ok, everything is stopped */
	oldpool = conf->r1bio_pool;
	conf->r1bio_pool = newpool;

	for (d = d2 = 0; d < conf->raid_disks; d++) {
		struct md_rdev *rdev = conf->mirrors[d].rdev;
		if (rdev && rdev->raid_disk != d2) {
			sysfs_unlink_rdev(mddev, rdev);
			rdev->raid_disk = d2;
			sysfs_unlink_rdev(mddev, rdev);
			if (sysfs_link_rdev(mddev, rdev))
				pr_warn("md/raid1:%s: cannot register rd%d\n",
					mdname(mddev), rdev->raid_disk);
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

	unfreeze_array(conf);

	set_bit(MD_RECOVERY_RECOVER, &mddev->recovery);
	set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	md_wakeup_thread(mddev->thread);

	mempool_exit(&oldpool);
	return 0;
}

static void raid1_quiesce(struct mddev *mddev, int quiesce)
{
	struct r1conf *conf = mddev->private;

	if (quiesce)
		freeze_array(conf, 0);
	else
		unfreeze_array(conf);
}

static void *raid1_takeover(struct mddev *mddev)
{
	/* raid1 can take over:
	 *  raid5 with 2 devices, any layout or chunk size
	 */
	if (mddev->level == 5 && mddev->raid_disks == 2) {
		struct r1conf *conf;
		mddev->new_level = 1;
		mddev->new_layout = 0;
		mddev->new_chunk_sectors = 0;
		conf = setup_conf(mddev);
		if (!IS_ERR(conf)) {
			/* Array must appear to be quiesced */
			conf->array_frozen = 1;
			mddev_clear_unsupported_flags(mddev,
				UNSUPPORTED_MDDEV_FLAGS);
		}
		return conf;
	}
	return ERR_PTR(-EINVAL);
}

static struct md_personality raid1_personality =
{
	.name		= "raid1",
	.level		= 1,
	.owner		= THIS_MODULE,
	.make_request	= raid1_make_request,
	.run		= raid1_run,
	.free		= raid1_free,
	.status		= raid1_status,
	.error_handler	= raid1_error,
	.hot_add_disk	= raid1_add_disk,
	.hot_remove_disk= raid1_remove_disk,
	.spare_active	= raid1_spare_active,
	.sync_request	= raid1_sync_request,
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
