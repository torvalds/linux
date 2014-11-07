/*
 * raid10.c : Multiple Devices driver for Linux
 *
 * Copyright (C) 2000-2004 Neil Brown
 *
 * RAID-10 support for md.
 *
 * Base on code in raid1.c.  See raid1.c for further copyright information.
 *
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
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/ratelimit.h>
#include <linux/kthread.h>
#include "md.h"
#include "raid10.h"
#include "raid0.h"
#include "bitmap.h"

/*
 * RAID10 provides a combination of RAID0 and RAID1 functionality.
 * The layout of data is defined by
 *    chunk_size
 *    raid_disks
 *    near_copies (stored in low byte of layout)
 *    far_copies (stored in second byte of layout)
 *    far_offset (stored in bit 16 of layout )
 *    use_far_sets (stored in bit 17 of layout )
 *
 * The data to be stored is divided into chunks using chunksize.  Each device
 * is divided into far_copies sections.   In each section, chunks are laid out
 * in a style similar to raid0, but near_copies copies of each chunk is stored
 * (each on a different drive).  The starting device for each section is offset
 * near_copies from the starting device of the previous section.  Thus there
 * are (near_copies * far_copies) of each chunk, and each is on a different
 * drive.  near_copies and far_copies must be at least one, and their product
 * is at most raid_disks.
 *
 * If far_offset is true, then the far_copies are handled a bit differently.
 * The copies are still in different stripes, but instead of being very far
 * apart on disk, there are adjacent stripes.
 *
 * The far and offset algorithms are handled slightly differently if
 * 'use_far_sets' is true.  In this case, the array's devices are grouped into
 * sets that are (near_copies * far_copies) in size.  The far copied stripes
 * are still shifted by 'near_copies' devices, but this shifting stays confined
 * to the set rather than the entire array.  This is done to improve the number
 * of device combinations that can fail without causing the array to fail.
 * Example 'far' algorithm w/o 'use_far_sets' (each letter represents a chunk
 * on a device):
 *    A B C D    A B C D E
 *      ...         ...
 *    D A B C    E A B C D
 * Example 'far' algorithm w/ 'use_far_sets' enabled (sets illustrated w/ []'s):
 *    [A B] [C D]    [A B] [C D E]
 *    |...| |...|    |...| | ... |
 *    [B A] [D C]    [B A] [E C D]
 */

/*
 * Number of guaranteed r10bios in case of extreme VM load:
 */
#define	NR_RAID10_BIOS 256

/* when we get a read error on a read-only array, we redirect to another
 * device without failing the first device, or trying to over-write to
 * correct the read error.  To keep track of bad blocks on a per-bio
 * level, we store IO_BLOCKED in the appropriate 'bios' pointer
 */
#define IO_BLOCKED ((struct bio *)1)
/* When we successfully write to a known bad-block, we need to remove the
 * bad-block marking which must be done from process context.  So we record
 * the success by setting devs[n].bio to IO_MADE_GOOD
 */
#define IO_MADE_GOOD ((struct bio *)2)

#define BIO_SPECIAL(bio) ((unsigned long)bio <= 2)

/* When there are this many requests queued to be written by
 * the raid10 thread, we become 'congested' to provide back-pressure
 * for writeback.
 */
static int max_queued_requests = 1024;

static void allow_barrier(struct r10conf *conf);
static void lower_barrier(struct r10conf *conf);
static int _enough(struct r10conf *conf, int previous, int ignore);
static sector_t reshape_request(struct mddev *mddev, sector_t sector_nr,
				int *skipped);
static void reshape_request_write(struct mddev *mddev, struct r10bio *r10_bio);
static void end_reshape_write(struct bio *bio, int error);
static void end_reshape(struct r10conf *conf);

static void * r10bio_pool_alloc(gfp_t gfp_flags, void *data)
{
	struct r10conf *conf = data;
	int size = offsetof(struct r10bio, devs[conf->copies]);

	/* allocate a r10bio with room for raid_disks entries in the
	 * bios array */
	return kzalloc(size, gfp_flags);
}

static void r10bio_pool_free(void *r10_bio, void *data)
{
	kfree(r10_bio);
}

/* Maximum size of each resync request */
#define RESYNC_BLOCK_SIZE (64*1024)
#define RESYNC_PAGES ((RESYNC_BLOCK_SIZE + PAGE_SIZE-1) / PAGE_SIZE)
/* amount of memory to reserve for resync requests */
#define RESYNC_WINDOW (1024*1024)
/* maximum number of concurrent requests, memory permitting */
#define RESYNC_DEPTH (32*1024*1024/RESYNC_BLOCK_SIZE)

/*
 * When performing a resync, we need to read and compare, so
 * we need as many pages are there are copies.
 * When performing a recovery, we need 2 bios, one for read,
 * one for write (we recover only one drive per r10buf)
 *
 */
static void * r10buf_pool_alloc(gfp_t gfp_flags, void *data)
{
	struct r10conf *conf = data;
	struct page *page;
	struct r10bio *r10_bio;
	struct bio *bio;
	int i, j;
	int nalloc;

	r10_bio = r10bio_pool_alloc(gfp_flags, conf);
	if (!r10_bio)
		return NULL;

	if (test_bit(MD_RECOVERY_SYNC, &conf->mddev->recovery) ||
	    test_bit(MD_RECOVERY_RESHAPE, &conf->mddev->recovery))
		nalloc = conf->copies; /* resync */
	else
		nalloc = 2; /* recovery */

	/*
	 * Allocate bios.
	 */
	for (j = nalloc ; j-- ; ) {
		bio = bio_kmalloc(gfp_flags, RESYNC_PAGES);
		if (!bio)
			goto out_free_bio;
		r10_bio->devs[j].bio = bio;
		if (!conf->have_replacement)
			continue;
		bio = bio_kmalloc(gfp_flags, RESYNC_PAGES);
		if (!bio)
			goto out_free_bio;
		r10_bio->devs[j].repl_bio = bio;
	}
	/*
	 * Allocate RESYNC_PAGES data pages and attach them
	 * where needed.
	 */
	for (j = 0 ; j < nalloc; j++) {
		struct bio *rbio = r10_bio->devs[j].repl_bio;
		bio = r10_bio->devs[j].bio;
		for (i = 0; i < RESYNC_PAGES; i++) {
			if (j > 0 && !test_bit(MD_RECOVERY_SYNC,
					       &conf->mddev->recovery)) {
				/* we can share bv_page's during recovery
				 * and reshape */
				struct bio *rbio = r10_bio->devs[0].bio;
				page = rbio->bi_io_vec[i].bv_page;
				get_page(page);
			} else
				page = alloc_page(gfp_flags);
			if (unlikely(!page))
				goto out_free_pages;

			bio->bi_io_vec[i].bv_page = page;
			if (rbio)
				rbio->bi_io_vec[i].bv_page = page;
		}
	}

	return r10_bio;

out_free_pages:
	for ( ; i > 0 ; i--)
		safe_put_page(bio->bi_io_vec[i-1].bv_page);
	while (j--)
		for (i = 0; i < RESYNC_PAGES ; i++)
			safe_put_page(r10_bio->devs[j].bio->bi_io_vec[i].bv_page);
	j = 0;
out_free_bio:
	for ( ; j < nalloc; j++) {
		if (r10_bio->devs[j].bio)
			bio_put(r10_bio->devs[j].bio);
		if (r10_bio->devs[j].repl_bio)
			bio_put(r10_bio->devs[j].repl_bio);
	}
	r10bio_pool_free(r10_bio, conf);
	return NULL;
}

static void r10buf_pool_free(void *__r10_bio, void *data)
{
	int i;
	struct r10conf *conf = data;
	struct r10bio *r10bio = __r10_bio;
	int j;

	for (j=0; j < conf->copies; j++) {
		struct bio *bio = r10bio->devs[j].bio;
		if (bio) {
			for (i = 0; i < RESYNC_PAGES; i++) {
				safe_put_page(bio->bi_io_vec[i].bv_page);
				bio->bi_io_vec[i].bv_page = NULL;
			}
			bio_put(bio);
		}
		bio = r10bio->devs[j].repl_bio;
		if (bio)
			bio_put(bio);
	}
	r10bio_pool_free(r10bio, conf);
}

static void put_all_bios(struct r10conf *conf, struct r10bio *r10_bio)
{
	int i;

	for (i = 0; i < conf->copies; i++) {
		struct bio **bio = & r10_bio->devs[i].bio;
		if (!BIO_SPECIAL(*bio))
			bio_put(*bio);
		*bio = NULL;
		bio = &r10_bio->devs[i].repl_bio;
		if (r10_bio->read_slot < 0 && !BIO_SPECIAL(*bio))
			bio_put(*bio);
		*bio = NULL;
	}
}

static void free_r10bio(struct r10bio *r10_bio)
{
	struct r10conf *conf = r10_bio->mddev->private;

	put_all_bios(conf, r10_bio);
	mempool_free(r10_bio, conf->r10bio_pool);
}

static void put_buf(struct r10bio *r10_bio)
{
	struct r10conf *conf = r10_bio->mddev->private;

	mempool_free(r10_bio, conf->r10buf_pool);

	lower_barrier(conf);
}

static void reschedule_retry(struct r10bio *r10_bio)
{
	unsigned long flags;
	struct mddev *mddev = r10_bio->mddev;
	struct r10conf *conf = mddev->private;

	spin_lock_irqsave(&conf->device_lock, flags);
	list_add(&r10_bio->retry_list, &conf->retry_list);
	conf->nr_queued ++;
	spin_unlock_irqrestore(&conf->device_lock, flags);

	/* wake up frozen array... */
	wake_up(&conf->wait_barrier);

	md_wakeup_thread(mddev->thread);
}

/*
 * raid_end_bio_io() is called when we have finished servicing a mirrored
 * operation and are ready to return a success/failure code to the buffer
 * cache layer.
 */
static void raid_end_bio_io(struct r10bio *r10_bio)
{
	struct bio *bio = r10_bio->master_bio;
	int done;
	struct r10conf *conf = r10_bio->mddev->private;

	if (bio->bi_phys_segments) {
		unsigned long flags;
		spin_lock_irqsave(&conf->device_lock, flags);
		bio->bi_phys_segments--;
		done = (bio->bi_phys_segments == 0);
		spin_unlock_irqrestore(&conf->device_lock, flags);
	} else
		done = 1;
	if (!test_bit(R10BIO_Uptodate, &r10_bio->state))
		clear_bit(BIO_UPTODATE, &bio->bi_flags);
	if (done) {
		bio_endio(bio, 0);
		/*
		 * Wake up any possible resync thread that waits for the device
		 * to go idle.
		 */
		allow_barrier(conf);
	}
	free_r10bio(r10_bio);
}

/*
 * Update disk head position estimator based on IRQ completion info.
 */
static inline void update_head_pos(int slot, struct r10bio *r10_bio)
{
	struct r10conf *conf = r10_bio->mddev->private;

	conf->mirrors[r10_bio->devs[slot].devnum].head_position =
		r10_bio->devs[slot].addr + (r10_bio->sectors);
}

/*
 * Find the disk number which triggered given bio
 */
static int find_bio_disk(struct r10conf *conf, struct r10bio *r10_bio,
			 struct bio *bio, int *slotp, int *replp)
{
	int slot;
	int repl = 0;

	for (slot = 0; slot < conf->copies; slot++) {
		if (r10_bio->devs[slot].bio == bio)
			break;
		if (r10_bio->devs[slot].repl_bio == bio) {
			repl = 1;
			break;
		}
	}

	BUG_ON(slot == conf->copies);
	update_head_pos(slot, r10_bio);

	if (slotp)
		*slotp = slot;
	if (replp)
		*replp = repl;
	return r10_bio->devs[slot].devnum;
}

static void raid10_end_read_request(struct bio *bio, int error)
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct r10bio *r10_bio = bio->bi_private;
	int slot, dev;
	struct md_rdev *rdev;
	struct r10conf *conf = r10_bio->mddev->private;

	slot = r10_bio->read_slot;
	dev = r10_bio->devs[slot].devnum;
	rdev = r10_bio->devs[slot].rdev;
	/*
	 * this branch is our 'one mirror IO has finished' event handler:
	 */
	update_head_pos(slot, r10_bio);

	if (uptodate) {
		/*
		 * Set R10BIO_Uptodate in our master bio, so that
		 * we will return a good error code to the higher
		 * levels even if IO on some other mirrored buffer fails.
		 *
		 * The 'master' represents the composite IO operation to
		 * user-side. So if something waits for IO, then it will
		 * wait for the 'master' bio.
		 */
		set_bit(R10BIO_Uptodate, &r10_bio->state);
	} else {
		/* If all other devices that store this block have
		 * failed, we want to return the error upwards rather
		 * than fail the last device.  Here we redefine
		 * "uptodate" to mean "Don't want to retry"
		 */
		if (!_enough(conf, test_bit(R10BIO_Previous, &r10_bio->state),
			     rdev->raid_disk))
			uptodate = 1;
	}
	if (uptodate) {
		raid_end_bio_io(r10_bio);
		rdev_dec_pending(rdev, conf->mddev);
	} else {
		/*
		 * oops, read error - keep the refcount on the rdev
		 */
		char b[BDEVNAME_SIZE];
		printk_ratelimited(KERN_ERR
				   "md/raid10:%s: %s: rescheduling sector %llu\n",
				   mdname(conf->mddev),
				   bdevname(rdev->bdev, b),
				   (unsigned long long)r10_bio->sector);
		set_bit(R10BIO_ReadError, &r10_bio->state);
		reschedule_retry(r10_bio);
	}
}

static void close_write(struct r10bio *r10_bio)
{
	/* clear the bitmap if all writes complete successfully */
	bitmap_endwrite(r10_bio->mddev->bitmap, r10_bio->sector,
			r10_bio->sectors,
			!test_bit(R10BIO_Degraded, &r10_bio->state),
			0);
	md_write_end(r10_bio->mddev);
}

static void one_write_done(struct r10bio *r10_bio)
{
	if (atomic_dec_and_test(&r10_bio->remaining)) {
		if (test_bit(R10BIO_WriteError, &r10_bio->state))
			reschedule_retry(r10_bio);
		else {
			close_write(r10_bio);
			if (test_bit(R10BIO_MadeGood, &r10_bio->state))
				reschedule_retry(r10_bio);
			else
				raid_end_bio_io(r10_bio);
		}
	}
}

static void raid10_end_write_request(struct bio *bio, int error)
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct r10bio *r10_bio = bio->bi_private;
	int dev;
	int dec_rdev = 1;
	struct r10conf *conf = r10_bio->mddev->private;
	int slot, repl;
	struct md_rdev *rdev = NULL;

	dev = find_bio_disk(conf, r10_bio, bio, &slot, &repl);

	if (repl)
		rdev = conf->mirrors[dev].replacement;
	if (!rdev) {
		smp_rmb();
		repl = 0;
		rdev = conf->mirrors[dev].rdev;
	}
	/*
	 * this branch is our 'one mirror IO has finished' event handler:
	 */
	if (!uptodate) {
		if (repl)
			/* Never record new bad blocks to replacement,
			 * just fail it.
			 */
			md_error(rdev->mddev, rdev);
		else {
			set_bit(WriteErrorSeen,	&rdev->flags);
			if (!test_and_set_bit(WantReplacement, &rdev->flags))
				set_bit(MD_RECOVERY_NEEDED,
					&rdev->mddev->recovery);
			set_bit(R10BIO_WriteError, &r10_bio->state);
			dec_rdev = 0;
		}
	} else {
		/*
		 * Set R10BIO_Uptodate in our master bio, so that
		 * we will return a good error code for to the higher
		 * levels even if IO on some other mirrored buffer fails.
		 *
		 * The 'master' represents the composite IO operation to
		 * user-side. So if something waits for IO, then it will
		 * wait for the 'master' bio.
		 */
		sector_t first_bad;
		int bad_sectors;

		/*
		 * Do not set R10BIO_Uptodate if the current device is
		 * rebuilding or Faulty. This is because we cannot use
		 * such device for properly reading the data back (we could
		 * potentially use it, if the current write would have felt
		 * before rdev->recovery_offset, but for simplicity we don't
		 * check this here.
		 */
		if (test_bit(In_sync, &rdev->flags) &&
		    !test_bit(Faulty, &rdev->flags))
			set_bit(R10BIO_Uptodate, &r10_bio->state);

		/* Maybe we can clear some bad blocks. */
		if (is_badblock(rdev,
				r10_bio->devs[slot].addr,
				r10_bio->sectors,
				&first_bad, &bad_sectors)) {
			bio_put(bio);
			if (repl)
				r10_bio->devs[slot].repl_bio = IO_MADE_GOOD;
			else
				r10_bio->devs[slot].bio = IO_MADE_GOOD;
			dec_rdev = 0;
			set_bit(R10BIO_MadeGood, &r10_bio->state);
		}
	}

	/*
	 *
	 * Let's see if all mirrored write operations have finished
	 * already.
	 */
	one_write_done(r10_bio);
	if (dec_rdev)
		rdev_dec_pending(rdev, conf->mddev);
}

/*
 * RAID10 layout manager
 * As well as the chunksize and raid_disks count, there are two
 * parameters: near_copies and far_copies.
 * near_copies * far_copies must be <= raid_disks.
 * Normally one of these will be 1.
 * If both are 1, we get raid0.
 * If near_copies == raid_disks, we get raid1.
 *
 * Chunks are laid out in raid0 style with near_copies copies of the
 * first chunk, followed by near_copies copies of the next chunk and
 * so on.
 * If far_copies > 1, then after 1/far_copies of the array has been assigned
 * as described above, we start again with a device offset of near_copies.
 * So we effectively have another copy of the whole array further down all
 * the drives, but with blocks on different drives.
 * With this layout, and block is never stored twice on the one device.
 *
 * raid10_find_phys finds the sector offset of a given virtual sector
 * on each device that it is on.
 *
 * raid10_find_virt does the reverse mapping, from a device and a
 * sector offset to a virtual address
 */

static void __raid10_find_phys(struct geom *geo, struct r10bio *r10bio)
{
	int n,f;
	sector_t sector;
	sector_t chunk;
	sector_t stripe;
	int dev;
	int slot = 0;
	int last_far_set_start, last_far_set_size;

	last_far_set_start = (geo->raid_disks / geo->far_set_size) - 1;
	last_far_set_start *= geo->far_set_size;

	last_far_set_size = geo->far_set_size;
	last_far_set_size += (geo->raid_disks % geo->far_set_size);

	/* now calculate first sector/dev */
	chunk = r10bio->sector >> geo->chunk_shift;
	sector = r10bio->sector & geo->chunk_mask;

	chunk *= geo->near_copies;
	stripe = chunk;
	dev = sector_div(stripe, geo->raid_disks);
	if (geo->far_offset)
		stripe *= geo->far_copies;

	sector += stripe << geo->chunk_shift;

	/* and calculate all the others */
	for (n = 0; n < geo->near_copies; n++) {
		int d = dev;
		int set;
		sector_t s = sector;
		r10bio->devs[slot].devnum = d;
		r10bio->devs[slot].addr = s;
		slot++;

		for (f = 1; f < geo->far_copies; f++) {
			set = d / geo->far_set_size;
			d += geo->near_copies;

			if ((geo->raid_disks % geo->far_set_size) &&
			    (d > last_far_set_start)) {
				d -= last_far_set_start;
				d %= last_far_set_size;
				d += last_far_set_start;
			} else {
				d %= geo->far_set_size;
				d += geo->far_set_size * set;
			}
			s += geo->stride;
			r10bio->devs[slot].devnum = d;
			r10bio->devs[slot].addr = s;
			slot++;
		}
		dev++;
		if (dev >= geo->raid_disks) {
			dev = 0;
			sector += (geo->chunk_mask + 1);
		}
	}
}

static void raid10_find_phys(struct r10conf *conf, struct r10bio *r10bio)
{
	struct geom *geo = &conf->geo;

	if (conf->reshape_progress != MaxSector &&
	    ((r10bio->sector >= conf->reshape_progress) !=
	     conf->mddev->reshape_backwards)) {
		set_bit(R10BIO_Previous, &r10bio->state);
		geo = &conf->prev;
	} else
		clear_bit(R10BIO_Previous, &r10bio->state);

	__raid10_find_phys(geo, r10bio);
}

static sector_t raid10_find_virt(struct r10conf *conf, sector_t sector, int dev)
{
	sector_t offset, chunk, vchunk;
	/* Never use conf->prev as this is only called during resync
	 * or recovery, so reshape isn't happening
	 */
	struct geom *geo = &conf->geo;
	int far_set_start = (dev / geo->far_set_size) * geo->far_set_size;
	int far_set_size = geo->far_set_size;
	int last_far_set_start;

	if (geo->raid_disks % geo->far_set_size) {
		last_far_set_start = (geo->raid_disks / geo->far_set_size) - 1;
		last_far_set_start *= geo->far_set_size;

		if (dev >= last_far_set_start) {
			far_set_size = geo->far_set_size;
			far_set_size += (geo->raid_disks % geo->far_set_size);
			far_set_start = last_far_set_start;
		}
	}

	offset = sector & geo->chunk_mask;
	if (geo->far_offset) {
		int fc;
		chunk = sector >> geo->chunk_shift;
		fc = sector_div(chunk, geo->far_copies);
		dev -= fc * geo->near_copies;
		if (dev < far_set_start)
			dev += far_set_size;
	} else {
		while (sector >= geo->stride) {
			sector -= geo->stride;
			if (dev < (geo->near_copies + far_set_start))
				dev += far_set_size - geo->near_copies;
			else
				dev -= geo->near_copies;
		}
		chunk = sector >> geo->chunk_shift;
	}
	vchunk = chunk * geo->raid_disks + dev;
	sector_div(vchunk, geo->near_copies);
	return (vchunk << geo->chunk_shift) + offset;
}

/**
 *	raid10_mergeable_bvec -- tell bio layer if a two requests can be merged
 *	@q: request queue
 *	@bvm: properties of new bio
 *	@biovec: the request that could be merged to it.
 *
 *	Return amount of bytes we can accept at this offset
 *	This requires checking for end-of-chunk if near_copies != raid_disks,
 *	and for subordinate merge_bvec_fns if merge_check_needed.
 */
static int raid10_mergeable_bvec(struct request_queue *q,
				 struct bvec_merge_data *bvm,
				 struct bio_vec *biovec)
{
	struct mddev *mddev = q->queuedata;
	struct r10conf *conf = mddev->private;
	sector_t sector = bvm->bi_sector + get_start_sect(bvm->bi_bdev);
	int max;
	unsigned int chunk_sectors;
	unsigned int bio_sectors = bvm->bi_size >> 9;
	struct geom *geo = &conf->geo;

	chunk_sectors = (conf->geo.chunk_mask & conf->prev.chunk_mask) + 1;
	if (conf->reshape_progress != MaxSector &&
	    ((sector >= conf->reshape_progress) !=
	     conf->mddev->reshape_backwards))
		geo = &conf->prev;

	if (geo->near_copies < geo->raid_disks) {
		max = (chunk_sectors - ((sector & (chunk_sectors - 1))
					+ bio_sectors)) << 9;
		if (max < 0)
			/* bio_add cannot handle a negative return */
			max = 0;
		if (max <= biovec->bv_len && bio_sectors == 0)
			return biovec->bv_len;
	} else
		max = biovec->bv_len;

	if (mddev->merge_check_needed) {
		struct {
			struct r10bio r10_bio;
			struct r10dev devs[conf->copies];
		} on_stack;
		struct r10bio *r10_bio = &on_stack.r10_bio;
		int s;
		if (conf->reshape_progress != MaxSector) {
			/* Cannot give any guidance during reshape */
			if (max <= biovec->bv_len && bio_sectors == 0)
				return biovec->bv_len;
			return 0;
		}
		r10_bio->sector = sector;
		raid10_find_phys(conf, r10_bio);
		rcu_read_lock();
		for (s = 0; s < conf->copies; s++) {
			int disk = r10_bio->devs[s].devnum;
			struct md_rdev *rdev = rcu_dereference(
				conf->mirrors[disk].rdev);
			if (rdev && !test_bit(Faulty, &rdev->flags)) {
				struct request_queue *q =
					bdev_get_queue(rdev->bdev);
				if (q->merge_bvec_fn) {
					bvm->bi_sector = r10_bio->devs[s].addr
						+ rdev->data_offset;
					bvm->bi_bdev = rdev->bdev;
					max = min(max, q->merge_bvec_fn(
							  q, bvm, biovec));
				}
			}
			rdev = rcu_dereference(conf->mirrors[disk].replacement);
			if (rdev && !test_bit(Faulty, &rdev->flags)) {
				struct request_queue *q =
					bdev_get_queue(rdev->bdev);
				if (q->merge_bvec_fn) {
					bvm->bi_sector = r10_bio->devs[s].addr
						+ rdev->data_offset;
					bvm->bi_bdev = rdev->bdev;
					max = min(max, q->merge_bvec_fn(
							  q, bvm, biovec));
				}
			}
		}
		rcu_read_unlock();
	}
	return max;
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

/*
 * FIXME: possibly should rethink readbalancing and do it differently
 * depending on near_copies / far_copies geometry.
 */
static struct md_rdev *read_balance(struct r10conf *conf,
				    struct r10bio *r10_bio,
				    int *max_sectors)
{
	const sector_t this_sector = r10_bio->sector;
	int disk, slot;
	int sectors = r10_bio->sectors;
	int best_good_sectors;
	sector_t new_distance, best_dist;
	struct md_rdev *best_rdev, *rdev = NULL;
	int do_balance;
	int best_slot;
	struct geom *geo = &conf->geo;

	raid10_find_phys(conf, r10_bio);
	rcu_read_lock();
retry:
	sectors = r10_bio->sectors;
	best_slot = -1;
	best_rdev = NULL;
	best_dist = MaxSector;
	best_good_sectors = 0;
	do_balance = 1;
	/*
	 * Check if we can balance. We can balance on the whole
	 * device if no resync is going on (recovery is ok), or below
	 * the resync window. We take the first readable disk when
	 * above the resync window.
	 */
	if (conf->mddev->recovery_cp < MaxSector
	    && (this_sector + sectors >= conf->next_resync))
		do_balance = 0;

	for (slot = 0; slot < conf->copies ; slot++) {
		sector_t first_bad;
		int bad_sectors;
		sector_t dev_sector;

		if (r10_bio->devs[slot].bio == IO_BLOCKED)
			continue;
		disk = r10_bio->devs[slot].devnum;
		rdev = rcu_dereference(conf->mirrors[disk].replacement);
		if (rdev == NULL || test_bit(Faulty, &rdev->flags) ||
		    test_bit(Unmerged, &rdev->flags) ||
		    r10_bio->devs[slot].addr + sectors > rdev->recovery_offset)
			rdev = rcu_dereference(conf->mirrors[disk].rdev);
		if (rdev == NULL ||
		    test_bit(Faulty, &rdev->flags) ||
		    test_bit(Unmerged, &rdev->flags))
			continue;
		if (!test_bit(In_sync, &rdev->flags) &&
		    r10_bio->devs[slot].addr + sectors > rdev->recovery_offset)
			continue;

		dev_sector = r10_bio->devs[slot].addr;
		if (is_badblock(rdev, dev_sector, sectors,
				&first_bad, &bad_sectors)) {
			if (best_dist < MaxSector)
				/* Already have a better slot */
				continue;
			if (first_bad <= dev_sector) {
				/* Cannot read here.  If this is the
				 * 'primary' device, then we must not read
				 * beyond 'bad_sectors' from another device.
				 */
				bad_sectors -= (dev_sector - first_bad);
				if (!do_balance && sectors > bad_sectors)
					sectors = bad_sectors;
				if (best_good_sectors > sectors)
					best_good_sectors = sectors;
			} else {
				sector_t good_sectors =
					first_bad - dev_sector;
				if (good_sectors > best_good_sectors) {
					best_good_sectors = good_sectors;
					best_slot = slot;
					best_rdev = rdev;
				}
				if (!do_balance)
					/* Must read from here */
					break;
			}
			continue;
		} else
			best_good_sectors = sectors;

		if (!do_balance)
			break;

		/* This optimisation is debatable, and completely destroys
		 * sequential read speed for 'far copies' arrays.  So only
		 * keep it for 'near' arrays, and review those later.
		 */
		if (geo->near_copies > 1 && !atomic_read(&rdev->nr_pending))
			break;

		/* for far > 1 always use the lowest address */
		if (geo->far_copies > 1)
			new_distance = r10_bio->devs[slot].addr;
		else
			new_distance = abs(r10_bio->devs[slot].addr -
					   conf->mirrors[disk].head_position);
		if (new_distance < best_dist) {
			best_dist = new_distance;
			best_slot = slot;
			best_rdev = rdev;
		}
	}
	if (slot >= conf->copies) {
		slot = best_slot;
		rdev = best_rdev;
	}

	if (slot >= 0) {
		atomic_inc(&rdev->nr_pending);
		if (test_bit(Faulty, &rdev->flags)) {
			/* Cannot risk returning a device that failed
			 * before we inc'ed nr_pending
			 */
			rdev_dec_pending(rdev, conf->mddev);
			goto retry;
		}
		r10_bio->read_slot = slot;
	} else
		rdev = NULL;
	rcu_read_unlock();
	*max_sectors = best_good_sectors;

	return rdev;
}

int md_raid10_congested(struct mddev *mddev, int bits)
{
	struct r10conf *conf = mddev->private;
	int i, ret = 0;

	if ((bits & (1 << BDI_async_congested)) &&
	    conf->pending_count >= max_queued_requests)
		return 1;

	rcu_read_lock();
	for (i = 0;
	     (i < conf->geo.raid_disks || i < conf->prev.raid_disks)
		     && ret == 0;
	     i++) {
		struct md_rdev *rdev = rcu_dereference(conf->mirrors[i].rdev);
		if (rdev && !test_bit(Faulty, &rdev->flags)) {
			struct request_queue *q = bdev_get_queue(rdev->bdev);

			ret |= bdi_congested(&q->backing_dev_info, bits);
		}
	}
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(md_raid10_congested);

static int raid10_congested(void *data, int bits)
{
	struct mddev *mddev = data;

	return mddev_congested(mddev, bits) ||
		md_raid10_congested(mddev, bits);
}

static void flush_pending_writes(struct r10conf *conf)
{
	/* Any writes that have been queued but are awaiting
	 * bitmap updates get flushed here.
	 */
	spin_lock_irq(&conf->device_lock);

	if (conf->pending_bio_list.head) {
		struct bio *bio;
		bio = bio_list_get(&conf->pending_bio_list);
		conf->pending_count = 0;
		spin_unlock_irq(&conf->device_lock);
		/* flush any pending bitmap writes to disk
		 * before proceeding w/ I/O */
		bitmap_unplug(conf->mddev->bitmap);
		wake_up(&conf->wait_barrier);

		while (bio) { /* submit pending writes */
			struct bio *next = bio->bi_next;
			bio->bi_next = NULL;
			if (unlikely((bio->bi_rw & REQ_DISCARD) &&
			    !blk_queue_discard(bdev_get_queue(bio->bi_bdev))))
				/* Just ignore it */
				bio_endio(bio, 0);
			else
				generic_make_request(bio);
			bio = next;
		}
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
 */

static void raise_barrier(struct r10conf *conf, int force)
{
	BUG_ON(force && !conf->barrier);
	spin_lock_irq(&conf->resync_lock);

	/* Wait until no block IO is waiting (unless 'force') */
	wait_event_lock_irq(conf->wait_barrier, force || !conf->nr_waiting,
			    conf->resync_lock);

	/* block any new IO from starting */
	conf->barrier++;

	/* Now wait for all pending IO to complete */
	wait_event_lock_irq(conf->wait_barrier,
			    !conf->nr_pending && conf->barrier < RESYNC_DEPTH,
			    conf->resync_lock);

	spin_unlock_irq(&conf->resync_lock);
}

static void lower_barrier(struct r10conf *conf)
{
	unsigned long flags;
	spin_lock_irqsave(&conf->resync_lock, flags);
	conf->barrier--;
	spin_unlock_irqrestore(&conf->resync_lock, flags);
	wake_up(&conf->wait_barrier);
}

static void wait_barrier(struct r10conf *conf)
{
	spin_lock_irq(&conf->resync_lock);
	if (conf->barrier) {
		conf->nr_waiting++;
		/* Wait for the barrier to drop.
		 * However if there are already pending
		 * requests (preventing the barrier from
		 * rising completely), and the
		 * pre-process bio queue isn't empty,
		 * then don't wait, as we need to empty
		 * that queue to get the nr_pending
		 * count down.
		 */
		wait_event_lock_irq(conf->wait_barrier,
				    !conf->barrier ||
				    (conf->nr_pending &&
				     current->bio_list &&
				     !bio_list_empty(current->bio_list)),
				    conf->resync_lock);
		conf->nr_waiting--;
	}
	conf->nr_pending++;
	spin_unlock_irq(&conf->resync_lock);
}

static void allow_barrier(struct r10conf *conf)
{
	unsigned long flags;
	spin_lock_irqsave(&conf->resync_lock, flags);
	conf->nr_pending--;
	spin_unlock_irqrestore(&conf->resync_lock, flags);
	wake_up(&conf->wait_barrier);
}

static void freeze_array(struct r10conf *conf, int extra)
{
	/* stop syncio and normal IO and wait for everything to
	 * go quiet.
	 * We increment barrier and nr_waiting, and then
	 * wait until nr_pending match nr_queued+extra
	 * This is called in the context of one normal IO request
	 * that has failed. Thus any sync request that might be pending
	 * will be blocked by nr_pending, and we need to wait for
	 * pending IO requests to complete or be queued for re-try.
	 * Thus the number queued (nr_queued) plus this request (extra)
	 * must match the number of pending IOs (nr_pending) before
	 * we continue.
	 */
	spin_lock_irq(&conf->resync_lock);
	conf->barrier++;
	conf->nr_waiting++;
	wait_event_lock_irq_cmd(conf->wait_barrier,
				conf->nr_pending == conf->nr_queued+extra,
				conf->resync_lock,
				flush_pending_writes(conf));

	spin_unlock_irq(&conf->resync_lock);
}

static void unfreeze_array(struct r10conf *conf)
{
	/* reverse the effect of the freeze */
	spin_lock_irq(&conf->resync_lock);
	conf->barrier--;
	conf->nr_waiting--;
	wake_up(&conf->wait_barrier);
	spin_unlock_irq(&conf->resync_lock);
}

static sector_t choose_data_offset(struct r10bio *r10_bio,
				   struct md_rdev *rdev)
{
	if (!test_bit(MD_RECOVERY_RESHAPE, &rdev->mddev->recovery) ||
	    test_bit(R10BIO_Previous, &r10_bio->state))
		return rdev->data_offset;
	else
		return rdev->new_data_offset;
}

struct raid10_plug_cb {
	struct blk_plug_cb	cb;
	struct bio_list		pending;
	int			pending_cnt;
};

static void raid10_unplug(struct blk_plug_cb *cb, bool from_schedule)
{
	struct raid10_plug_cb *plug = container_of(cb, struct raid10_plug_cb,
						   cb);
	struct mddev *mddev = plug->cb.data;
	struct r10conf *conf = mddev->private;
	struct bio *bio;

	if (from_schedule || current->bio_list) {
		spin_lock_irq(&conf->device_lock);
		bio_list_merge(&conf->pending_bio_list, &plug->pending);
		conf->pending_count += plug->pending_cnt;
		spin_unlock_irq(&conf->device_lock);
		wake_up(&conf->wait_barrier);
		md_wakeup_thread(mddev->thread);
		kfree(plug);
		return;
	}

	/* we aren't scheduling, so we can do the write-out directly. */
	bio = bio_list_get(&plug->pending);
	bitmap_unplug(mddev->bitmap);
	wake_up(&conf->wait_barrier);

	while (bio) { /* submit pending writes */
		struct bio *next = bio->bi_next;
		bio->bi_next = NULL;
		if (unlikely((bio->bi_rw & REQ_DISCARD) &&
		    !blk_queue_discard(bdev_get_queue(bio->bi_bdev))))
			/* Just ignore it */
			bio_endio(bio, 0);
		else
			generic_make_request(bio);
		bio = next;
	}
	kfree(plug);
}

static void __make_request(struct mddev *mddev, struct bio *bio)
{
	struct r10conf *conf = mddev->private;
	struct r10bio *r10_bio;
	struct bio *read_bio;
	int i;
	const int rw = bio_data_dir(bio);
	const unsigned long do_sync = (bio->bi_rw & REQ_SYNC);
	const unsigned long do_fua = (bio->bi_rw & REQ_FUA);
	const unsigned long do_discard = (bio->bi_rw
					  & (REQ_DISCARD | REQ_SECURE));
	const unsigned long do_same = (bio->bi_rw & REQ_WRITE_SAME);
	unsigned long flags;
	struct md_rdev *blocked_rdev;
	struct blk_plug_cb *cb;
	struct raid10_plug_cb *plug = NULL;
	int sectors_handled;
	int max_sectors;
	int sectors;

	/*
	 * Register the new request and wait if the reconstruction
	 * thread has put up a bar for new requests.
	 * Continue immediately if no resync is active currently.
	 */
	wait_barrier(conf);

	sectors = bio_sectors(bio);
	while (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery) &&
	    bio->bi_iter.bi_sector < conf->reshape_progress &&
	    bio->bi_iter.bi_sector + sectors > conf->reshape_progress) {
		/* IO spans the reshape position.  Need to wait for
		 * reshape to pass
		 */
		allow_barrier(conf);
		wait_event(conf->wait_barrier,
			   conf->reshape_progress <= bio->bi_iter.bi_sector ||
			   conf->reshape_progress >= bio->bi_iter.bi_sector +
			   sectors);
		wait_barrier(conf);
	}
	if (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery) &&
	    bio_data_dir(bio) == WRITE &&
	    (mddev->reshape_backwards
	     ? (bio->bi_iter.bi_sector < conf->reshape_safe &&
		bio->bi_iter.bi_sector + sectors > conf->reshape_progress)
	     : (bio->bi_iter.bi_sector + sectors > conf->reshape_safe &&
		bio->bi_iter.bi_sector < conf->reshape_progress))) {
		/* Need to update reshape_position in metadata */
		mddev->reshape_position = conf->reshape_progress;
		set_bit(MD_CHANGE_DEVS, &mddev->flags);
		set_bit(MD_CHANGE_PENDING, &mddev->flags);
		md_wakeup_thread(mddev->thread);
		wait_event(mddev->sb_wait,
			   !test_bit(MD_CHANGE_PENDING, &mddev->flags));

		conf->reshape_safe = mddev->reshape_position;
	}

	r10_bio = mempool_alloc(conf->r10bio_pool, GFP_NOIO);

	r10_bio->master_bio = bio;
	r10_bio->sectors = sectors;

	r10_bio->mddev = mddev;
	r10_bio->sector = bio->bi_iter.bi_sector;
	r10_bio->state = 0;

	/* We might need to issue multiple reads to different
	 * devices if there are bad blocks around, so we keep
	 * track of the number of reads in bio->bi_phys_segments.
	 * If this is 0, there is only one r10_bio and no locking
	 * will be needed when the request completes.  If it is
	 * non-zero, then it is the number of not-completed requests.
	 */
	bio->bi_phys_segments = 0;
	clear_bit(BIO_SEG_VALID, &bio->bi_flags);

	if (rw == READ) {
		/*
		 * read balancing logic:
		 */
		struct md_rdev *rdev;
		int slot;

read_again:
		rdev = read_balance(conf, r10_bio, &max_sectors);
		if (!rdev) {
			raid_end_bio_io(r10_bio);
			return;
		}
		slot = r10_bio->read_slot;

		read_bio = bio_clone_mddev(bio, GFP_NOIO, mddev);
		bio_trim(read_bio, r10_bio->sector - bio->bi_iter.bi_sector,
			 max_sectors);

		r10_bio->devs[slot].bio = read_bio;
		r10_bio->devs[slot].rdev = rdev;

		read_bio->bi_iter.bi_sector = r10_bio->devs[slot].addr +
			choose_data_offset(r10_bio, rdev);
		read_bio->bi_bdev = rdev->bdev;
		read_bio->bi_end_io = raid10_end_read_request;
		read_bio->bi_rw = READ | do_sync;
		read_bio->bi_private = r10_bio;

		if (max_sectors < r10_bio->sectors) {
			/* Could not read all from this device, so we will
			 * need another r10_bio.
			 */
			sectors_handled = (r10_bio->sector + max_sectors
					   - bio->bi_iter.bi_sector);
			r10_bio->sectors = max_sectors;
			spin_lock_irq(&conf->device_lock);
			if (bio->bi_phys_segments == 0)
				bio->bi_phys_segments = 2;
			else
				bio->bi_phys_segments++;
			spin_unlock_irq(&conf->device_lock);
			/* Cannot call generic_make_request directly
			 * as that will be queued in __generic_make_request
			 * and subsequent mempool_alloc might block
			 * waiting for it.  so hand bio over to raid10d.
			 */
			reschedule_retry(r10_bio);

			r10_bio = mempool_alloc(conf->r10bio_pool, GFP_NOIO);

			r10_bio->master_bio = bio;
			r10_bio->sectors = bio_sectors(bio) - sectors_handled;
			r10_bio->state = 0;
			r10_bio->mddev = mddev;
			r10_bio->sector = bio->bi_iter.bi_sector +
				sectors_handled;
			goto read_again;
		} else
			generic_make_request(read_bio);
		return;
	}

	/*
	 * WRITE:
	 */
	if (conf->pending_count >= max_queued_requests) {
		md_wakeup_thread(mddev->thread);
		wait_event(conf->wait_barrier,
			   conf->pending_count < max_queued_requests);
	}
	/* first select target devices under rcu_lock and
	 * inc refcount on their rdev.  Record them by setting
	 * bios[x] to bio
	 * If there are known/acknowledged bad blocks on any device
	 * on which we have seen a write error, we want to avoid
	 * writing to those blocks.  This potentially requires several
	 * writes to write around the bad blocks.  Each set of writes
	 * gets its own r10_bio with a set of bios attached.  The number
	 * of r10_bios is recored in bio->bi_phys_segments just as with
	 * the read case.
	 */

	r10_bio->read_slot = -1; /* make sure repl_bio gets freed */
	raid10_find_phys(conf, r10_bio);
retry_write:
	blocked_rdev = NULL;
	rcu_read_lock();
	max_sectors = r10_bio->sectors;

	for (i = 0;  i < conf->copies; i++) {
		int d = r10_bio->devs[i].devnum;
		struct md_rdev *rdev = rcu_dereference(conf->mirrors[d].rdev);
		struct md_rdev *rrdev = rcu_dereference(
			conf->mirrors[d].replacement);
		if (rdev == rrdev)
			rrdev = NULL;
		if (rdev && unlikely(test_bit(Blocked, &rdev->flags))) {
			atomic_inc(&rdev->nr_pending);
			blocked_rdev = rdev;
			break;
		}
		if (rrdev && unlikely(test_bit(Blocked, &rrdev->flags))) {
			atomic_inc(&rrdev->nr_pending);
			blocked_rdev = rrdev;
			break;
		}
		if (rdev && (test_bit(Faulty, &rdev->flags)
			     || test_bit(Unmerged, &rdev->flags)))
			rdev = NULL;
		if (rrdev && (test_bit(Faulty, &rrdev->flags)
			      || test_bit(Unmerged, &rrdev->flags)))
			rrdev = NULL;

		r10_bio->devs[i].bio = NULL;
		r10_bio->devs[i].repl_bio = NULL;

		if (!rdev && !rrdev) {
			set_bit(R10BIO_Degraded, &r10_bio->state);
			continue;
		}
		if (rdev && test_bit(WriteErrorSeen, &rdev->flags)) {
			sector_t first_bad;
			sector_t dev_sector = r10_bio->devs[i].addr;
			int bad_sectors;
			int is_bad;

			is_bad = is_badblock(rdev, dev_sector,
					     max_sectors,
					     &first_bad, &bad_sectors);
			if (is_bad < 0) {
				/* Mustn't write here until the bad block
				 * is acknowledged
				 */
				atomic_inc(&rdev->nr_pending);
				set_bit(BlockedBadBlocks, &rdev->flags);
				blocked_rdev = rdev;
				break;
			}
			if (is_bad && first_bad <= dev_sector) {
				/* Cannot write here at all */
				bad_sectors -= (dev_sector - first_bad);
				if (bad_sectors < max_sectors)
					/* Mustn't write more than bad_sectors
					 * to other devices yet
					 */
					max_sectors = bad_sectors;
				/* We don't set R10BIO_Degraded as that
				 * only applies if the disk is missing,
				 * so it might be re-added, and we want to
				 * know to recover this chunk.
				 * In this case the device is here, and the
				 * fact that this chunk is not in-sync is
				 * recorded in the bad block log.
				 */
				continue;
			}
			if (is_bad) {
				int good_sectors = first_bad - dev_sector;
				if (good_sectors < max_sectors)
					max_sectors = good_sectors;
			}
		}
		if (rdev) {
			r10_bio->devs[i].bio = bio;
			atomic_inc(&rdev->nr_pending);
		}
		if (rrdev) {
			r10_bio->devs[i].repl_bio = bio;
			atomic_inc(&rrdev->nr_pending);
		}
	}
	rcu_read_unlock();

	if (unlikely(blocked_rdev)) {
		/* Have to wait for this device to get unblocked, then retry */
		int j;
		int d;

		for (j = 0; j < i; j++) {
			if (r10_bio->devs[j].bio) {
				d = r10_bio->devs[j].devnum;
				rdev_dec_pending(conf->mirrors[d].rdev, mddev);
			}
			if (r10_bio->devs[j].repl_bio) {
				struct md_rdev *rdev;
				d = r10_bio->devs[j].devnum;
				rdev = conf->mirrors[d].replacement;
				if (!rdev) {
					/* Race with remove_disk */
					smp_mb();
					rdev = conf->mirrors[d].rdev;
				}
				rdev_dec_pending(rdev, mddev);
			}
		}
		allow_barrier(conf);
		md_wait_for_blocked_rdev(blocked_rdev, mddev);
		wait_barrier(conf);
		goto retry_write;
	}

	if (max_sectors < r10_bio->sectors) {
		/* We are splitting this into multiple parts, so
		 * we need to prepare for allocating another r10_bio.
		 */
		r10_bio->sectors = max_sectors;
		spin_lock_irq(&conf->device_lock);
		if (bio->bi_phys_segments == 0)
			bio->bi_phys_segments = 2;
		else
			bio->bi_phys_segments++;
		spin_unlock_irq(&conf->device_lock);
	}
	sectors_handled = r10_bio->sector + max_sectors -
		bio->bi_iter.bi_sector;

	atomic_set(&r10_bio->remaining, 1);
	bitmap_startwrite(mddev->bitmap, r10_bio->sector, r10_bio->sectors, 0);

	for (i = 0; i < conf->copies; i++) {
		struct bio *mbio;
		int d = r10_bio->devs[i].devnum;
		if (r10_bio->devs[i].bio) {
			struct md_rdev *rdev = conf->mirrors[d].rdev;
			mbio = bio_clone_mddev(bio, GFP_NOIO, mddev);
			bio_trim(mbio, r10_bio->sector - bio->bi_iter.bi_sector,
				 max_sectors);
			r10_bio->devs[i].bio = mbio;

			mbio->bi_iter.bi_sector	= (r10_bio->devs[i].addr+
					   choose_data_offset(r10_bio,
							      rdev));
			mbio->bi_bdev = rdev->bdev;
			mbio->bi_end_io	= raid10_end_write_request;
			mbio->bi_rw =
				WRITE | do_sync | do_fua | do_discard | do_same;
			mbio->bi_private = r10_bio;

			atomic_inc(&r10_bio->remaining);

			cb = blk_check_plugged(raid10_unplug, mddev,
					       sizeof(*plug));
			if (cb)
				plug = container_of(cb, struct raid10_plug_cb,
						    cb);
			else
				plug = NULL;
			spin_lock_irqsave(&conf->device_lock, flags);
			if (plug) {
				bio_list_add(&plug->pending, mbio);
				plug->pending_cnt++;
			} else {
				bio_list_add(&conf->pending_bio_list, mbio);
				conf->pending_count++;
			}
			spin_unlock_irqrestore(&conf->device_lock, flags);
			if (!plug)
				md_wakeup_thread(mddev->thread);
		}

		if (r10_bio->devs[i].repl_bio) {
			struct md_rdev *rdev = conf->mirrors[d].replacement;
			if (rdev == NULL) {
				/* Replacement just got moved to main 'rdev' */
				smp_mb();
				rdev = conf->mirrors[d].rdev;
			}
			mbio = bio_clone_mddev(bio, GFP_NOIO, mddev);
			bio_trim(mbio, r10_bio->sector - bio->bi_iter.bi_sector,
				 max_sectors);
			r10_bio->devs[i].repl_bio = mbio;

			mbio->bi_iter.bi_sector	= (r10_bio->devs[i].addr +
					   choose_data_offset(
						   r10_bio, rdev));
			mbio->bi_bdev = rdev->bdev;
			mbio->bi_end_io	= raid10_end_write_request;
			mbio->bi_rw =
				WRITE | do_sync | do_fua | do_discard | do_same;
			mbio->bi_private = r10_bio;

			atomic_inc(&r10_bio->remaining);
			spin_lock_irqsave(&conf->device_lock, flags);
			bio_list_add(&conf->pending_bio_list, mbio);
			conf->pending_count++;
			spin_unlock_irqrestore(&conf->device_lock, flags);
			if (!mddev_check_plugged(mddev))
				md_wakeup_thread(mddev->thread);
		}
	}

	/* Don't remove the bias on 'remaining' (one_write_done) until
	 * after checking if we need to go around again.
	 */

	if (sectors_handled < bio_sectors(bio)) {
		one_write_done(r10_bio);
		/* We need another r10_bio.  It has already been counted
		 * in bio->bi_phys_segments.
		 */
		r10_bio = mempool_alloc(conf->r10bio_pool, GFP_NOIO);

		r10_bio->master_bio = bio;
		r10_bio->sectors = bio_sectors(bio) - sectors_handled;

		r10_bio->mddev = mddev;
		r10_bio->sector = bio->bi_iter.bi_sector + sectors_handled;
		r10_bio->state = 0;
		goto retry_write;
	}
	one_write_done(r10_bio);
}

static void make_request(struct mddev *mddev, struct bio *bio)
{
	struct r10conf *conf = mddev->private;
	sector_t chunk_mask = (conf->geo.chunk_mask & conf->prev.chunk_mask);
	int chunk_sects = chunk_mask + 1;

	struct bio *split;

	if (unlikely(bio->bi_rw & REQ_FLUSH)) {
		md_flush_request(mddev, bio);
		return;
	}

	md_write_start(mddev, bio);

	do {

		/*
		 * If this request crosses a chunk boundary, we need to split
		 * it.
		 */
		if (unlikely((bio->bi_iter.bi_sector & chunk_mask) +
			     bio_sectors(bio) > chunk_sects
			     && (conf->geo.near_copies < conf->geo.raid_disks
				 || conf->prev.near_copies <
				 conf->prev.raid_disks))) {
			split = bio_split(bio, chunk_sects -
					  (bio->bi_iter.bi_sector &
					   (chunk_sects - 1)),
					  GFP_NOIO, fs_bio_set);
			bio_chain(split, bio);
		} else {
			split = bio;
		}

		__make_request(mddev, split);
	} while (split != bio);

	/* In case raid10d snuck in to freeze_array */
	wake_up(&conf->wait_barrier);
}

static void status(struct seq_file *seq, struct mddev *mddev)
{
	struct r10conf *conf = mddev->private;
	int i;

	if (conf->geo.near_copies < conf->geo.raid_disks)
		seq_printf(seq, " %dK chunks", mddev->chunk_sectors / 2);
	if (conf->geo.near_copies > 1)
		seq_printf(seq, " %d near-copies", conf->geo.near_copies);
	if (conf->geo.far_copies > 1) {
		if (conf->geo.far_offset)
			seq_printf(seq, " %d offset-copies", conf->geo.far_copies);
		else
			seq_printf(seq, " %d far-copies", conf->geo.far_copies);
	}
	seq_printf(seq, " [%d/%d] [", conf->geo.raid_disks,
					conf->geo.raid_disks - mddev->degraded);
	for (i = 0; i < conf->geo.raid_disks; i++)
		seq_printf(seq, "%s",
			      conf->mirrors[i].rdev &&
			      test_bit(In_sync, &conf->mirrors[i].rdev->flags) ? "U" : "_");
	seq_printf(seq, "]");
}

/* check if there are enough drives for
 * every block to appear on atleast one.
 * Don't consider the device numbered 'ignore'
 * as we might be about to remove it.
 */
static int _enough(struct r10conf *conf, int previous, int ignore)
{
	int first = 0;
	int has_enough = 0;
	int disks, ncopies;
	if (previous) {
		disks = conf->prev.raid_disks;
		ncopies = conf->prev.near_copies;
	} else {
		disks = conf->geo.raid_disks;
		ncopies = conf->geo.near_copies;
	}

	rcu_read_lock();
	do {
		int n = conf->copies;
		int cnt = 0;
		int this = first;
		while (n--) {
			struct md_rdev *rdev;
			if (this != ignore &&
			    (rdev = rcu_dereference(conf->mirrors[this].rdev)) &&
			    test_bit(In_sync, &rdev->flags))
				cnt++;
			this = (this+1) % disks;
		}
		if (cnt == 0)
			goto out;
		first = (first + ncopies) % disks;
	} while (first != 0);
	has_enough = 1;
out:
	rcu_read_unlock();
	return has_enough;
}

static int enough(struct r10conf *conf, int ignore)
{
	/* when calling 'enough', both 'prev' and 'geo' must
	 * be stable.
	 * This is ensured if ->reconfig_mutex or ->device_lock
	 * is held.
	 */
	return _enough(conf, 0, ignore) &&
		_enough(conf, 1, ignore);
}

static void error(struct mddev *mddev, struct md_rdev *rdev)
{
	char b[BDEVNAME_SIZE];
	struct r10conf *conf = mddev->private;
	unsigned long flags;

	/*
	 * If it is not operational, then we have already marked it as dead
	 * else if it is the last working disks, ignore the error, let the
	 * next level up know.
	 * else mark the drive as failed
	 */
	spin_lock_irqsave(&conf->device_lock, flags);
	if (test_bit(In_sync, &rdev->flags)
	    && !enough(conf, rdev->raid_disk)) {
		/*
		 * Don't fail the drive, just return an IO error.
		 */
		spin_unlock_irqrestore(&conf->device_lock, flags);
		return;
	}
	if (test_and_clear_bit(In_sync, &rdev->flags))
		mddev->degraded++;
	/*
	 * If recovery is running, make sure it aborts.
	 */
	set_bit(MD_RECOVERY_INTR, &mddev->recovery);
	set_bit(Blocked, &rdev->flags);
	set_bit(Faulty, &rdev->flags);
	set_bit(MD_CHANGE_DEVS, &mddev->flags);
	spin_unlock_irqrestore(&conf->device_lock, flags);
	printk(KERN_ALERT
	       "md/raid10:%s: Disk failure on %s, disabling device.\n"
	       "md/raid10:%s: Operation continuing on %d devices.\n",
	       mdname(mddev), bdevname(rdev->bdev, b),
	       mdname(mddev), conf->geo.raid_disks - mddev->degraded);
}

static void print_conf(struct r10conf *conf)
{
	int i;
	struct raid10_info *tmp;

	printk(KERN_DEBUG "RAID10 conf printout:\n");
	if (!conf) {
		printk(KERN_DEBUG "(!conf)\n");
		return;
	}
	printk(KERN_DEBUG " --- wd:%d rd:%d\n", conf->geo.raid_disks - conf->mddev->degraded,
		conf->geo.raid_disks);

	for (i = 0; i < conf->geo.raid_disks; i++) {
		char b[BDEVNAME_SIZE];
		tmp = conf->mirrors + i;
		if (tmp->rdev)
			printk(KERN_DEBUG " disk %d, wo:%d, o:%d, dev:%s\n",
				i, !test_bit(In_sync, &tmp->rdev->flags),
			        !test_bit(Faulty, &tmp->rdev->flags),
				bdevname(tmp->rdev->bdev,b));
	}
}

static void close_sync(struct r10conf *conf)
{
	wait_barrier(conf);
	allow_barrier(conf);

	mempool_destroy(conf->r10buf_pool);
	conf->r10buf_pool = NULL;
}

static int raid10_spare_active(struct mddev *mddev)
{
	int i;
	struct r10conf *conf = mddev->private;
	struct raid10_info *tmp;
	int count = 0;
	unsigned long flags;

	/*
	 * Find all non-in_sync disks within the RAID10 configuration
	 * and mark them in_sync
	 */
	for (i = 0; i < conf->geo.raid_disks; i++) {
		tmp = conf->mirrors + i;
		if (tmp->replacement
		    && tmp->replacement->recovery_offset == MaxSector
		    && !test_bit(Faulty, &tmp->replacement->flags)
		    && !test_and_set_bit(In_sync, &tmp->replacement->flags)) {
			/* Replacement has just become active */
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
	mddev->degraded -= count;
	spin_unlock_irqrestore(&conf->device_lock, flags);

	print_conf(conf);
	return count;
}

static int raid10_add_disk(struct mddev *mddev, struct md_rdev *rdev)
{
	struct r10conf *conf = mddev->private;
	int err = -EEXIST;
	int mirror;
	int first = 0;
	int last = conf->geo.raid_disks - 1;
	struct request_queue *q = bdev_get_queue(rdev->bdev);

	if (mddev->recovery_cp < MaxSector)
		/* only hot-add to in-sync arrays, as recovery is
		 * very different from resync
		 */
		return -EBUSY;
	if (rdev->saved_raid_disk < 0 && !_enough(conf, 1, -1))
		return -EINVAL;

	if (rdev->raid_disk >= 0)
		first = last = rdev->raid_disk;

	if (q->merge_bvec_fn) {
		set_bit(Unmerged, &rdev->flags);
		mddev->merge_check_needed = 1;
	}

	if (rdev->saved_raid_disk >= first &&
	    conf->mirrors[rdev->saved_raid_disk].rdev == NULL)
		mirror = rdev->saved_raid_disk;
	else
		mirror = first;
	for ( ; mirror <= last ; mirror++) {
		struct raid10_info *p = &conf->mirrors[mirror];
		if (p->recovery_disabled == mddev->recovery_disabled)
			continue;
		if (p->rdev) {
			if (!test_bit(WantReplacement, &p->rdev->flags) ||
			    p->replacement != NULL)
				continue;
			clear_bit(In_sync, &rdev->flags);
			set_bit(Replacement, &rdev->flags);
			rdev->raid_disk = mirror;
			err = 0;
			if (mddev->gendisk)
				disk_stack_limits(mddev->gendisk, rdev->bdev,
						  rdev->data_offset << 9);
			conf->fullsync = 1;
			rcu_assign_pointer(p->replacement, rdev);
			break;
		}

		if (mddev->gendisk)
			disk_stack_limits(mddev->gendisk, rdev->bdev,
					  rdev->data_offset << 9);

		p->head_position = 0;
		p->recovery_disabled = mddev->recovery_disabled - 1;
		rdev->raid_disk = mirror;
		err = 0;
		if (rdev->saved_raid_disk != mirror)
			conf->fullsync = 1;
		rcu_assign_pointer(p->rdev, rdev);
		break;
	}
	if (err == 0 && test_bit(Unmerged, &rdev->flags)) {
		/* Some requests might not have seen this new
		 * merge_bvec_fn.  We must wait for them to complete
		 * before merging the device fully.
		 * First we make sure any code which has tested
		 * our function has submitted the request, then
		 * we wait for all outstanding requests to complete.
		 */
		synchronize_sched();
		freeze_array(conf, 0);
		unfreeze_array(conf);
		clear_bit(Unmerged, &rdev->flags);
	}
	md_integrity_add_rdev(rdev, mddev);
	if (mddev->queue && blk_queue_discard(bdev_get_queue(rdev->bdev)))
		queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, mddev->queue);

	print_conf(conf);
	return err;
}

static int raid10_remove_disk(struct mddev *mddev, struct md_rdev *rdev)
{
	struct r10conf *conf = mddev->private;
	int err = 0;
	int number = rdev->raid_disk;
	struct md_rdev **rdevp;
	struct raid10_info *p = conf->mirrors + number;

	print_conf(conf);
	if (rdev == p->rdev)
		rdevp = &p->rdev;
	else if (rdev == p->replacement)
		rdevp = &p->replacement;
	else
		return 0;

	if (test_bit(In_sync, &rdev->flags) ||
	    atomic_read(&rdev->nr_pending)) {
		err = -EBUSY;
		goto abort;
	}
	/* Only remove faulty devices if recovery
	 * is not possible.
	 */
	if (!test_bit(Faulty, &rdev->flags) &&
	    mddev->recovery_disabled != p->recovery_disabled &&
	    (!p->replacement || p->replacement == rdev) &&
	    number < conf->geo.raid_disks &&
	    enough(conf, -1)) {
		err = -EBUSY;
		goto abort;
	}
	*rdevp = NULL;
	synchronize_rcu();
	if (atomic_read(&rdev->nr_pending)) {
		/* lost the race, try later */
		err = -EBUSY;
		*rdevp = rdev;
		goto abort;
	} else if (p->replacement) {
		/* We must have just cleared 'rdev' */
		p->rdev = p->replacement;
		clear_bit(Replacement, &p->replacement->flags);
		smp_mb(); /* Make sure other CPUs may see both as identical
			   * but will never see neither -- if they are careful.
			   */
		p->replacement = NULL;
		clear_bit(WantReplacement, &rdev->flags);
	} else
		/* We might have just remove the Replacement as faulty
		 * Clear the flag just in case
		 */
		clear_bit(WantReplacement, &rdev->flags);

	err = md_integrity_register(mddev);

abort:

	print_conf(conf);
	return err;
}

static void end_sync_read(struct bio *bio, int error)
{
	struct r10bio *r10_bio = bio->bi_private;
	struct r10conf *conf = r10_bio->mddev->private;
	int d;

	if (bio == r10_bio->master_bio) {
		/* this is a reshape read */
		d = r10_bio->read_slot; /* really the read dev */
	} else
		d = find_bio_disk(conf, r10_bio, bio, NULL, NULL);

	if (test_bit(BIO_UPTODATE, &bio->bi_flags))
		set_bit(R10BIO_Uptodate, &r10_bio->state);
	else
		/* The write handler will notice the lack of
		 * R10BIO_Uptodate and record any errors etc
		 */
		atomic_add(r10_bio->sectors,
			   &conf->mirrors[d].rdev->corrected_errors);

	/* for reconstruct, we always reschedule after a read.
	 * for resync, only after all reads
	 */
	rdev_dec_pending(conf->mirrors[d].rdev, conf->mddev);
	if (test_bit(R10BIO_IsRecover, &r10_bio->state) ||
	    atomic_dec_and_test(&r10_bio->remaining)) {
		/* we have read all the blocks,
		 * do the comparison in process context in raid10d
		 */
		reschedule_retry(r10_bio);
	}
}

static void end_sync_request(struct r10bio *r10_bio)
{
	struct mddev *mddev = r10_bio->mddev;

	while (atomic_dec_and_test(&r10_bio->remaining)) {
		if (r10_bio->master_bio == NULL) {
			/* the primary of several recovery bios */
			sector_t s = r10_bio->sectors;
			if (test_bit(R10BIO_MadeGood, &r10_bio->state) ||
			    test_bit(R10BIO_WriteError, &r10_bio->state))
				reschedule_retry(r10_bio);
			else
				put_buf(r10_bio);
			md_done_sync(mddev, s, 1);
			break;
		} else {
			struct r10bio *r10_bio2 = (struct r10bio *)r10_bio->master_bio;
			if (test_bit(R10BIO_MadeGood, &r10_bio->state) ||
			    test_bit(R10BIO_WriteError, &r10_bio->state))
				reschedule_retry(r10_bio);
			else
				put_buf(r10_bio);
			r10_bio = r10_bio2;
		}
	}
}

static void end_sync_write(struct bio *bio, int error)
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct r10bio *r10_bio = bio->bi_private;
	struct mddev *mddev = r10_bio->mddev;
	struct r10conf *conf = mddev->private;
	int d;
	sector_t first_bad;
	int bad_sectors;
	int slot;
	int repl;
	struct md_rdev *rdev = NULL;

	d = find_bio_disk(conf, r10_bio, bio, &slot, &repl);
	if (repl)
		rdev = conf->mirrors[d].replacement;
	else
		rdev = conf->mirrors[d].rdev;

	if (!uptodate) {
		if (repl)
			md_error(mddev, rdev);
		else {
			set_bit(WriteErrorSeen, &rdev->flags);
			if (!test_and_set_bit(WantReplacement, &rdev->flags))
				set_bit(MD_RECOVERY_NEEDED,
					&rdev->mddev->recovery);
			set_bit(R10BIO_WriteError, &r10_bio->state);
		}
	} else if (is_badblock(rdev,
			     r10_bio->devs[slot].addr,
			     r10_bio->sectors,
			     &first_bad, &bad_sectors))
		set_bit(R10BIO_MadeGood, &r10_bio->state);

	rdev_dec_pending(rdev, mddev);

	end_sync_request(r10_bio);
}

/*
 * Note: sync and recover and handled very differently for raid10
 * This code is for resync.
 * For resync, we read through virtual addresses and read all blocks.
 * If there is any error, we schedule a write.  The lowest numbered
 * drive is authoritative.
 * However requests come for physical address, so we need to map.
 * For every physical address there are raid_disks/copies virtual addresses,
 * which is always are least one, but is not necessarly an integer.
 * This means that a physical address can span multiple chunks, so we may
 * have to submit multiple io requests for a single sync request.
 */
/*
 * We check if all blocks are in-sync and only write to blocks that
 * aren't in sync
 */
static void sync_request_write(struct mddev *mddev, struct r10bio *r10_bio)
{
	struct r10conf *conf = mddev->private;
	int i, first;
	struct bio *tbio, *fbio;
	int vcnt;

	atomic_set(&r10_bio->remaining, 1);

	/* find the first device with a block */
	for (i=0; i<conf->copies; i++)
		if (test_bit(BIO_UPTODATE, &r10_bio->devs[i].bio->bi_flags))
			break;

	if (i == conf->copies)
		goto done;

	first = i;
	fbio = r10_bio->devs[i].bio;

	vcnt = (r10_bio->sectors + (PAGE_SIZE >> 9) - 1) >> (PAGE_SHIFT - 9);
	/* now find blocks with errors */
	for (i=0 ; i < conf->copies ; i++) {
		int  j, d;

		tbio = r10_bio->devs[i].bio;

		if (tbio->bi_end_io != end_sync_read)
			continue;
		if (i == first)
			continue;
		if (test_bit(BIO_UPTODATE, &r10_bio->devs[i].bio->bi_flags)) {
			/* We know that the bi_io_vec layout is the same for
			 * both 'first' and 'i', so we just compare them.
			 * All vec entries are PAGE_SIZE;
			 */
			int sectors = r10_bio->sectors;
			for (j = 0; j < vcnt; j++) {
				int len = PAGE_SIZE;
				if (sectors < (len / 512))
					len = sectors * 512;
				if (memcmp(page_address(fbio->bi_io_vec[j].bv_page),
					   page_address(tbio->bi_io_vec[j].bv_page),
					   len))
					break;
				sectors -= len/512;
			}
			if (j == vcnt)
				continue;
			atomic64_add(r10_bio->sectors, &mddev->resync_mismatches);
			if (test_bit(MD_RECOVERY_CHECK, &mddev->recovery))
				/* Don't fix anything. */
				continue;
		}
		/* Ok, we need to write this bio, either to correct an
		 * inconsistency or to correct an unreadable block.
		 * First we need to fixup bv_offset, bv_len and
		 * bi_vecs, as the read request might have corrupted these
		 */
		bio_reset(tbio);

		tbio->bi_vcnt = vcnt;
		tbio->bi_iter.bi_size = r10_bio->sectors << 9;
		tbio->bi_rw = WRITE;
		tbio->bi_private = r10_bio;
		tbio->bi_iter.bi_sector = r10_bio->devs[i].addr;

		for (j=0; j < vcnt ; j++) {
			tbio->bi_io_vec[j].bv_offset = 0;
			tbio->bi_io_vec[j].bv_len = PAGE_SIZE;

			memcpy(page_address(tbio->bi_io_vec[j].bv_page),
			       page_address(fbio->bi_io_vec[j].bv_page),
			       PAGE_SIZE);
		}
		tbio->bi_end_io = end_sync_write;

		d = r10_bio->devs[i].devnum;
		atomic_inc(&conf->mirrors[d].rdev->nr_pending);
		atomic_inc(&r10_bio->remaining);
		md_sync_acct(conf->mirrors[d].rdev->bdev, bio_sectors(tbio));

		tbio->bi_iter.bi_sector += conf->mirrors[d].rdev->data_offset;
		tbio->bi_bdev = conf->mirrors[d].rdev->bdev;
		generic_make_request(tbio);
	}

	/* Now write out to any replacement devices
	 * that are active
	 */
	for (i = 0; i < conf->copies; i++) {
		int j, d;

		tbio = r10_bio->devs[i].repl_bio;
		if (!tbio || !tbio->bi_end_io)
			continue;
		if (r10_bio->devs[i].bio->bi_end_io != end_sync_write
		    && r10_bio->devs[i].bio != fbio)
			for (j = 0; j < vcnt; j++)
				memcpy(page_address(tbio->bi_io_vec[j].bv_page),
				       page_address(fbio->bi_io_vec[j].bv_page),
				       PAGE_SIZE);
		d = r10_bio->devs[i].devnum;
		atomic_inc(&r10_bio->remaining);
		md_sync_acct(conf->mirrors[d].replacement->bdev,
			     bio_sectors(tbio));
		generic_make_request(tbio);
	}

done:
	if (atomic_dec_and_test(&r10_bio->remaining)) {
		md_done_sync(mddev, r10_bio->sectors, 1);
		put_buf(r10_bio);
	}
}

/*
 * Now for the recovery code.
 * Recovery happens across physical sectors.
 * We recover all non-is_sync drives by finding the virtual address of
 * each, and then choose a working drive that also has that virt address.
 * There is a separate r10_bio for each non-in_sync drive.
 * Only the first two slots are in use. The first for reading,
 * The second for writing.
 *
 */
static void fix_recovery_read_error(struct r10bio *r10_bio)
{
	/* We got a read error during recovery.
	 * We repeat the read in smaller page-sized sections.
	 * If a read succeeds, write it to the new device or record
	 * a bad block if we cannot.
	 * If a read fails, record a bad block on both old and
	 * new devices.
	 */
	struct mddev *mddev = r10_bio->mddev;
	struct r10conf *conf = mddev->private;
	struct bio *bio = r10_bio->devs[0].bio;
	sector_t sect = 0;
	int sectors = r10_bio->sectors;
	int idx = 0;
	int dr = r10_bio->devs[0].devnum;
	int dw = r10_bio->devs[1].devnum;

	while (sectors) {
		int s = sectors;
		struct md_rdev *rdev;
		sector_t addr;
		int ok;

		if (s > (PAGE_SIZE>>9))
			s = PAGE_SIZE >> 9;

		rdev = conf->mirrors[dr].rdev;
		addr = r10_bio->devs[0].addr + sect,
		ok = sync_page_io(rdev,
				  addr,
				  s << 9,
				  bio->bi_io_vec[idx].bv_page,
				  READ, false);
		if (ok) {
			rdev = conf->mirrors[dw].rdev;
			addr = r10_bio->devs[1].addr + sect;
			ok = sync_page_io(rdev,
					  addr,
					  s << 9,
					  bio->bi_io_vec[idx].bv_page,
					  WRITE, false);
			if (!ok) {
				set_bit(WriteErrorSeen, &rdev->flags);
				if (!test_and_set_bit(WantReplacement,
						      &rdev->flags))
					set_bit(MD_RECOVERY_NEEDED,
						&rdev->mddev->recovery);
			}
		}
		if (!ok) {
			/* We don't worry if we cannot set a bad block -
			 * it really is bad so there is no loss in not
			 * recording it yet
			 */
			rdev_set_badblocks(rdev, addr, s, 0);

			if (rdev != conf->mirrors[dw].rdev) {
				/* need bad block on destination too */
				struct md_rdev *rdev2 = conf->mirrors[dw].rdev;
				addr = r10_bio->devs[1].addr + sect;
				ok = rdev_set_badblocks(rdev2, addr, s, 0);
				if (!ok) {
					/* just abort the recovery */
					printk(KERN_NOTICE
					       "md/raid10:%s: recovery aborted"
					       " due to read error\n",
					       mdname(mddev));

					conf->mirrors[dw].recovery_disabled
						= mddev->recovery_disabled;
					set_bit(MD_RECOVERY_INTR,
						&mddev->recovery);
					break;
				}
			}
		}

		sectors -= s;
		sect += s;
		idx++;
	}
}

static void recovery_request_write(struct mddev *mddev, struct r10bio *r10_bio)
{
	struct r10conf *conf = mddev->private;
	int d;
	struct bio *wbio, *wbio2;

	if (!test_bit(R10BIO_Uptodate, &r10_bio->state)) {
		fix_recovery_read_error(r10_bio);
		end_sync_request(r10_bio);
		return;
	}

	/*
	 * share the pages with the first bio
	 * and submit the write request
	 */
	d = r10_bio->devs[1].devnum;
	wbio = r10_bio->devs[1].bio;
	wbio2 = r10_bio->devs[1].repl_bio;
	/* Need to test wbio2->bi_end_io before we call
	 * generic_make_request as if the former is NULL,
	 * the latter is free to free wbio2.
	 */
	if (wbio2 && !wbio2->bi_end_io)
		wbio2 = NULL;
	if (wbio->bi_end_io) {
		atomic_inc(&conf->mirrors[d].rdev->nr_pending);
		md_sync_acct(conf->mirrors[d].rdev->bdev, bio_sectors(wbio));
		generic_make_request(wbio);
	}
	if (wbio2) {
		atomic_inc(&conf->mirrors[d].replacement->nr_pending);
		md_sync_acct(conf->mirrors[d].replacement->bdev,
			     bio_sectors(wbio2));
		generic_make_request(wbio2);
	}
}

/*
 * Used by fix_read_error() to decay the per rdev read_errors.
 * We halve the read error count for every hour that has elapsed
 * since the last recorded read error.
 *
 */
static void check_decay_read_errors(struct mddev *mddev, struct md_rdev *rdev)
{
	struct timespec cur_time_mon;
	unsigned long hours_since_last;
	unsigned int read_errors = atomic_read(&rdev->read_errors);

	ktime_get_ts(&cur_time_mon);

	if (rdev->last_read_error.tv_sec == 0 &&
	    rdev->last_read_error.tv_nsec == 0) {
		/* first time we've seen a read error */
		rdev->last_read_error = cur_time_mon;
		return;
	}

	hours_since_last = (cur_time_mon.tv_sec -
			    rdev->last_read_error.tv_sec) / 3600;

	rdev->last_read_error = cur_time_mon;

	/*
	 * if hours_since_last is > the number of bits in read_errors
	 * just set read errors to 0. We do this to avoid
	 * overflowing the shift of read_errors by hours_since_last.
	 */
	if (hours_since_last >= 8 * sizeof(read_errors))
		atomic_set(&rdev->read_errors, 0);
	else
		atomic_set(&rdev->read_errors, read_errors >> hours_since_last);
}

static int r10_sync_page_io(struct md_rdev *rdev, sector_t sector,
			    int sectors, struct page *page, int rw)
{
	sector_t first_bad;
	int bad_sectors;

	if (is_badblock(rdev, sector, sectors, &first_bad, &bad_sectors)
	    && (rw == READ || test_bit(WriteErrorSeen, &rdev->flags)))
		return -1;
	if (sync_page_io(rdev, sector, sectors << 9, page, rw, false))
		/* success */
		return 1;
	if (rw == WRITE) {
		set_bit(WriteErrorSeen, &rdev->flags);
		if (!test_and_set_bit(WantReplacement, &rdev->flags))
			set_bit(MD_RECOVERY_NEEDED,
				&rdev->mddev->recovery);
	}
	/* need to record an error - either for the block or the device */
	if (!rdev_set_badblocks(rdev, sector, sectors, 0))
		md_error(rdev->mddev, rdev);
	return 0;
}

/*
 * This is a kernel thread which:
 *
 *	1.	Retries failed read operations on working mirrors.
 *	2.	Updates the raid superblock when problems encounter.
 *	3.	Performs writes following reads for array synchronising.
 */

static void fix_read_error(struct r10conf *conf, struct mddev *mddev, struct r10bio *r10_bio)
{
	int sect = 0; /* Offset from r10_bio->sector */
	int sectors = r10_bio->sectors;
	struct md_rdev*rdev;
	int max_read_errors = atomic_read(&mddev->max_corr_read_errors);
	int d = r10_bio->devs[r10_bio->read_slot].devnum;

	/* still own a reference to this rdev, so it cannot
	 * have been cleared recently.
	 */
	rdev = conf->mirrors[d].rdev;

	if (test_bit(Faulty, &rdev->flags))
		/* drive has already been failed, just ignore any
		   more fix_read_error() attempts */
		return;

	check_decay_read_errors(mddev, rdev);
	atomic_inc(&rdev->read_errors);
	if (atomic_read(&rdev->read_errors) > max_read_errors) {
		char b[BDEVNAME_SIZE];
		bdevname(rdev->bdev, b);

		printk(KERN_NOTICE
		       "md/raid10:%s: %s: Raid device exceeded "
		       "read_error threshold [cur %d:max %d]\n",
		       mdname(mddev), b,
		       atomic_read(&rdev->read_errors), max_read_errors);
		printk(KERN_NOTICE
		       "md/raid10:%s: %s: Failing raid device\n",
		       mdname(mddev), b);
		md_error(mddev, conf->mirrors[d].rdev);
		r10_bio->devs[r10_bio->read_slot].bio = IO_BLOCKED;
		return;
	}

	while(sectors) {
		int s = sectors;
		int sl = r10_bio->read_slot;
		int success = 0;
		int start;

		if (s > (PAGE_SIZE>>9))
			s = PAGE_SIZE >> 9;

		rcu_read_lock();
		do {
			sector_t first_bad;
			int bad_sectors;

			d = r10_bio->devs[sl].devnum;
			rdev = rcu_dereference(conf->mirrors[d].rdev);
			if (rdev &&
			    !test_bit(Unmerged, &rdev->flags) &&
			    test_bit(In_sync, &rdev->flags) &&
			    is_badblock(rdev, r10_bio->devs[sl].addr + sect, s,
					&first_bad, &bad_sectors) == 0) {
				atomic_inc(&rdev->nr_pending);
				rcu_read_unlock();
				success = sync_page_io(rdev,
						       r10_bio->devs[sl].addr +
						       sect,
						       s<<9,
						       conf->tmppage, READ, false);
				rdev_dec_pending(rdev, mddev);
				rcu_read_lock();
				if (success)
					break;
			}
			sl++;
			if (sl == conf->copies)
				sl = 0;
		} while (!success && sl != r10_bio->read_slot);
		rcu_read_unlock();

		if (!success) {
			/* Cannot read from anywhere, just mark the block
			 * as bad on the first device to discourage future
			 * reads.
			 */
			int dn = r10_bio->devs[r10_bio->read_slot].devnum;
			rdev = conf->mirrors[dn].rdev;

			if (!rdev_set_badblocks(
				    rdev,
				    r10_bio->devs[r10_bio->read_slot].addr
				    + sect,
				    s, 0)) {
				md_error(mddev, rdev);
				r10_bio->devs[r10_bio->read_slot].bio
					= IO_BLOCKED;
			}
			break;
		}

		start = sl;
		/* write it back and re-read */
		rcu_read_lock();
		while (sl != r10_bio->read_slot) {
			char b[BDEVNAME_SIZE];

			if (sl==0)
				sl = conf->copies;
			sl--;
			d = r10_bio->devs[sl].devnum;
			rdev = rcu_dereference(conf->mirrors[d].rdev);
			if (!rdev ||
			    test_bit(Unmerged, &rdev->flags) ||
			    !test_bit(In_sync, &rdev->flags))
				continue;

			atomic_inc(&rdev->nr_pending);
			rcu_read_unlock();
			if (r10_sync_page_io(rdev,
					     r10_bio->devs[sl].addr +
					     sect,
					     s, conf->tmppage, WRITE)
			    == 0) {
				/* Well, this device is dead */
				printk(KERN_NOTICE
				       "md/raid10:%s: read correction "
				       "write failed"
				       " (%d sectors at %llu on %s)\n",
				       mdname(mddev), s,
				       (unsigned long long)(
					       sect +
					       choose_data_offset(r10_bio,
								  rdev)),
				       bdevname(rdev->bdev, b));
				printk(KERN_NOTICE "md/raid10:%s: %s: failing "
				       "drive\n",
				       mdname(mddev),
				       bdevname(rdev->bdev, b));
			}
			rdev_dec_pending(rdev, mddev);
			rcu_read_lock();
		}
		sl = start;
		while (sl != r10_bio->read_slot) {
			char b[BDEVNAME_SIZE];

			if (sl==0)
				sl = conf->copies;
			sl--;
			d = r10_bio->devs[sl].devnum;
			rdev = rcu_dereference(conf->mirrors[d].rdev);
			if (!rdev ||
			    !test_bit(In_sync, &rdev->flags))
				continue;

			atomic_inc(&rdev->nr_pending);
			rcu_read_unlock();
			switch (r10_sync_page_io(rdev,
					     r10_bio->devs[sl].addr +
					     sect,
					     s, conf->tmppage,
						 READ)) {
			case 0:
				/* Well, this device is dead */
				printk(KERN_NOTICE
				       "md/raid10:%s: unable to read back "
				       "corrected sectors"
				       " (%d sectors at %llu on %s)\n",
				       mdname(mddev), s,
				       (unsigned long long)(
					       sect +
					       choose_data_offset(r10_bio, rdev)),
				       bdevname(rdev->bdev, b));
				printk(KERN_NOTICE "md/raid10:%s: %s: failing "
				       "drive\n",
				       mdname(mddev),
				       bdevname(rdev->bdev, b));
				break;
			case 1:
				printk(KERN_INFO
				       "md/raid10:%s: read error corrected"
				       " (%d sectors at %llu on %s)\n",
				       mdname(mddev), s,
				       (unsigned long long)(
					       sect +
					       choose_data_offset(r10_bio, rdev)),
				       bdevname(rdev->bdev, b));
				atomic_add(s, &rdev->corrected_errors);
			}

			rdev_dec_pending(rdev, mddev);
			rcu_read_lock();
		}
		rcu_read_unlock();

		sectors -= s;
		sect += s;
	}
}

static int narrow_write_error(struct r10bio *r10_bio, int i)
{
	struct bio *bio = r10_bio->master_bio;
	struct mddev *mddev = r10_bio->mddev;
	struct r10conf *conf = mddev->private;
	struct md_rdev *rdev = conf->mirrors[r10_bio->devs[i].devnum].rdev;
	/* bio has the data to be written to slot 'i' where
	 * we just recently had a write error.
	 * We repeatedly clone the bio and trim down to one block,
	 * then try the write.  Where the write fails we record
	 * a bad block.
	 * It is conceivable that the bio doesn't exactly align with
	 * blocks.  We must handle this.
	 *
	 * We currently own a reference to the rdev.
	 */

	int block_sectors;
	sector_t sector;
	int sectors;
	int sect_to_write = r10_bio->sectors;
	int ok = 1;

	if (rdev->badblocks.shift < 0)
		return 0;

	block_sectors = 1 << rdev->badblocks.shift;
	sector = r10_bio->sector;
	sectors = ((r10_bio->sector + block_sectors)
		   & ~(sector_t)(block_sectors - 1))
		- sector;

	while (sect_to_write) {
		struct bio *wbio;
		if (sectors > sect_to_write)
			sectors = sect_to_write;
		/* Write at 'sector' for 'sectors' */
		wbio = bio_clone_mddev(bio, GFP_NOIO, mddev);
		bio_trim(wbio, sector - bio->bi_iter.bi_sector, sectors);
		wbio->bi_iter.bi_sector = (r10_bio->devs[i].addr+
				   choose_data_offset(r10_bio, rdev) +
				   (sector - r10_bio->sector));
		wbio->bi_bdev = rdev->bdev;
		if (submit_bio_wait(WRITE, wbio) == 0)
			/* Failure! */
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

static void handle_read_error(struct mddev *mddev, struct r10bio *r10_bio)
{
	int slot = r10_bio->read_slot;
	struct bio *bio;
	struct r10conf *conf = mddev->private;
	struct md_rdev *rdev = r10_bio->devs[slot].rdev;
	char b[BDEVNAME_SIZE];
	unsigned long do_sync;
	int max_sectors;

	/* we got a read error. Maybe the drive is bad.  Maybe just
	 * the block and we can fix it.
	 * We freeze all other IO, and try reading the block from
	 * other devices.  When we find one, we re-write
	 * and check it that fixes the read error.
	 * This is all done synchronously while the array is
	 * frozen.
	 */
	bio = r10_bio->devs[slot].bio;
	bdevname(bio->bi_bdev, b);
	bio_put(bio);
	r10_bio->devs[slot].bio = NULL;

	if (mddev->ro == 0) {
		freeze_array(conf, 1);
		fix_read_error(conf, mddev, r10_bio);
		unfreeze_array(conf);
	} else
		r10_bio->devs[slot].bio = IO_BLOCKED;

	rdev_dec_pending(rdev, mddev);

read_more:
	rdev = read_balance(conf, r10_bio, &max_sectors);
	if (rdev == NULL) {
		printk(KERN_ALERT "md/raid10:%s: %s: unrecoverable I/O"
		       " read error for block %llu\n",
		       mdname(mddev), b,
		       (unsigned long long)r10_bio->sector);
		raid_end_bio_io(r10_bio);
		return;
	}

	do_sync = (r10_bio->master_bio->bi_rw & REQ_SYNC);
	slot = r10_bio->read_slot;
	printk_ratelimited(
		KERN_ERR
		"md/raid10:%s: %s: redirecting "
		"sector %llu to another mirror\n",
		mdname(mddev),
		bdevname(rdev->bdev, b),
		(unsigned long long)r10_bio->sector);
	bio = bio_clone_mddev(r10_bio->master_bio,
			      GFP_NOIO, mddev);
	bio_trim(bio, r10_bio->sector - bio->bi_iter.bi_sector, max_sectors);
	r10_bio->devs[slot].bio = bio;
	r10_bio->devs[slot].rdev = rdev;
	bio->bi_iter.bi_sector = r10_bio->devs[slot].addr
		+ choose_data_offset(r10_bio, rdev);
	bio->bi_bdev = rdev->bdev;
	bio->bi_rw = READ | do_sync;
	bio->bi_private = r10_bio;
	bio->bi_end_io = raid10_end_read_request;
	if (max_sectors < r10_bio->sectors) {
		/* Drat - have to split this up more */
		struct bio *mbio = r10_bio->master_bio;
		int sectors_handled =
			r10_bio->sector + max_sectors
			- mbio->bi_iter.bi_sector;
		r10_bio->sectors = max_sectors;
		spin_lock_irq(&conf->device_lock);
		if (mbio->bi_phys_segments == 0)
			mbio->bi_phys_segments = 2;
		else
			mbio->bi_phys_segments++;
		spin_unlock_irq(&conf->device_lock);
		generic_make_request(bio);

		r10_bio = mempool_alloc(conf->r10bio_pool,
					GFP_NOIO);
		r10_bio->master_bio = mbio;
		r10_bio->sectors = bio_sectors(mbio) - sectors_handled;
		r10_bio->state = 0;
		set_bit(R10BIO_ReadError,
			&r10_bio->state);
		r10_bio->mddev = mddev;
		r10_bio->sector = mbio->bi_iter.bi_sector
			+ sectors_handled;

		goto read_more;
	} else
		generic_make_request(bio);
}

static void handle_write_completed(struct r10conf *conf, struct r10bio *r10_bio)
{
	/* Some sort of write request has finished and it
	 * succeeded in writing where we thought there was a
	 * bad block.  So forget the bad block.
	 * Or possibly if failed and we need to record
	 * a bad block.
	 */
	int m;
	struct md_rdev *rdev;

	if (test_bit(R10BIO_IsSync, &r10_bio->state) ||
	    test_bit(R10BIO_IsRecover, &r10_bio->state)) {
		for (m = 0; m < conf->copies; m++) {
			int dev = r10_bio->devs[m].devnum;
			rdev = conf->mirrors[dev].rdev;
			if (r10_bio->devs[m].bio == NULL)
				continue;
			if (test_bit(BIO_UPTODATE,
				     &r10_bio->devs[m].bio->bi_flags)) {
				rdev_clear_badblocks(
					rdev,
					r10_bio->devs[m].addr,
					r10_bio->sectors, 0);
			} else {
				if (!rdev_set_badblocks(
					    rdev,
					    r10_bio->devs[m].addr,
					    r10_bio->sectors, 0))
					md_error(conf->mddev, rdev);
			}
			rdev = conf->mirrors[dev].replacement;
			if (r10_bio->devs[m].repl_bio == NULL)
				continue;
			if (test_bit(BIO_UPTODATE,
				     &r10_bio->devs[m].repl_bio->bi_flags)) {
				rdev_clear_badblocks(
					rdev,
					r10_bio->devs[m].addr,
					r10_bio->sectors, 0);
			} else {
				if (!rdev_set_badblocks(
					    rdev,
					    r10_bio->devs[m].addr,
					    r10_bio->sectors, 0))
					md_error(conf->mddev, rdev);
			}
		}
		put_buf(r10_bio);
	} else {
		for (m = 0; m < conf->copies; m++) {
			int dev = r10_bio->devs[m].devnum;
			struct bio *bio = r10_bio->devs[m].bio;
			rdev = conf->mirrors[dev].rdev;
			if (bio == IO_MADE_GOOD) {
				rdev_clear_badblocks(
					rdev,
					r10_bio->devs[m].addr,
					r10_bio->sectors, 0);
				rdev_dec_pending(rdev, conf->mddev);
			} else if (bio != NULL &&
				   !test_bit(BIO_UPTODATE, &bio->bi_flags)) {
				if (!narrow_write_error(r10_bio, m)) {
					md_error(conf->mddev, rdev);
					set_bit(R10BIO_Degraded,
						&r10_bio->state);
				}
				rdev_dec_pending(rdev, conf->mddev);
			}
			bio = r10_bio->devs[m].repl_bio;
			rdev = conf->mirrors[dev].replacement;
			if (rdev && bio == IO_MADE_GOOD) {
				rdev_clear_badblocks(
					rdev,
					r10_bio->devs[m].addr,
					r10_bio->sectors, 0);
				rdev_dec_pending(rdev, conf->mddev);
			}
		}
		if (test_bit(R10BIO_WriteError,
			     &r10_bio->state))
			close_write(r10_bio);
		raid_end_bio_io(r10_bio);
	}
}

static void raid10d(struct md_thread *thread)
{
	struct mddev *mddev = thread->mddev;
	struct r10bio *r10_bio;
	unsigned long flags;
	struct r10conf *conf = mddev->private;
	struct list_head *head = &conf->retry_list;
	struct blk_plug plug;

	md_check_recovery(mddev);

	blk_start_plug(&plug);
	for (;;) {

		flush_pending_writes(conf);

		spin_lock_irqsave(&conf->device_lock, flags);
		if (list_empty(head)) {
			spin_unlock_irqrestore(&conf->device_lock, flags);
			break;
		}
		r10_bio = list_entry(head->prev, struct r10bio, retry_list);
		list_del(head->prev);
		conf->nr_queued--;
		spin_unlock_irqrestore(&conf->device_lock, flags);

		mddev = r10_bio->mddev;
		conf = mddev->private;
		if (test_bit(R10BIO_MadeGood, &r10_bio->state) ||
		    test_bit(R10BIO_WriteError, &r10_bio->state))
			handle_write_completed(conf, r10_bio);
		else if (test_bit(R10BIO_IsReshape, &r10_bio->state))
			reshape_request_write(mddev, r10_bio);
		else if (test_bit(R10BIO_IsSync, &r10_bio->state))
			sync_request_write(mddev, r10_bio);
		else if (test_bit(R10BIO_IsRecover, &r10_bio->state))
			recovery_request_write(mddev, r10_bio);
		else if (test_bit(R10BIO_ReadError, &r10_bio->state))
			handle_read_error(mddev, r10_bio);
		else {
			/* just a partial read to be scheduled from a
			 * separate context
			 */
			int slot = r10_bio->read_slot;
			generic_make_request(r10_bio->devs[slot].bio);
		}

		cond_resched();
		if (mddev->flags & ~(1<<MD_CHANGE_PENDING))
			md_check_recovery(mddev);
	}
	blk_finish_plug(&plug);
}

static int init_resync(struct r10conf *conf)
{
	int buffs;
	int i;

	buffs = RESYNC_WINDOW / RESYNC_BLOCK_SIZE;
	BUG_ON(conf->r10buf_pool);
	conf->have_replacement = 0;
	for (i = 0; i < conf->geo.raid_disks; i++)
		if (conf->mirrors[i].replacement)
			conf->have_replacement = 1;
	conf->r10buf_pool = mempool_create(buffs, r10buf_pool_alloc, r10buf_pool_free, conf);
	if (!conf->r10buf_pool)
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
 *
 * Resync and recovery are handled very differently.
 * We differentiate by looking at MD_RECOVERY_SYNC in mddev->recovery.
 *
 * For resync, we iterate over virtual addresses, read all copies,
 * and update if there are differences.  If only one copy is live,
 * skip it.
 * For recovery, we iterate over physical addresses, read a good
 * value for each non-in_sync drive, and over-write.
 *
 * So, for recovery we may have several outstanding complex requests for a
 * given address, one for each out-of-sync device.  We model this by allocating
 * a number of r10_bio structures, one for each out-of-sync device.
 * As we setup these structures, we collect all bio's together into a list
 * which we then process collectively to add pages, and then process again
 * to pass to generic_make_request.
 *
 * The r10_bio structures are linked using a borrowed master_bio pointer.
 * This link is counted in ->remaining.  When the r10_bio that points to NULL
 * has its remaining count decremented to 0, the whole complex operation
 * is complete.
 *
 */

static sector_t sync_request(struct mddev *mddev, sector_t sector_nr,
			     int *skipped, int go_faster)
{
	struct r10conf *conf = mddev->private;
	struct r10bio *r10_bio;
	struct bio *biolist = NULL, *bio;
	sector_t max_sector, nr_sectors;
	int i;
	int max_sync;
	sector_t sync_blocks;
	sector_t sectors_skipped = 0;
	int chunks_skipped = 0;
	sector_t chunk_mask = conf->geo.chunk_mask;

	if (!conf->r10buf_pool)
		if (init_resync(conf))
			return 0;

	/*
	 * Allow skipping a full rebuild for incremental assembly
	 * of a clean array, like RAID1 does.
	 */
	if (mddev->bitmap == NULL &&
	    mddev->recovery_cp == MaxSector &&
	    mddev->reshape_position == MaxSector &&
	    !test_bit(MD_RECOVERY_SYNC, &mddev->recovery) &&
	    !test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery) &&
	    !test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery) &&
	    conf->fullsync == 0) {
		*skipped = 1;
		return mddev->dev_sectors - sector_nr;
	}

 skipped:
	max_sector = mddev->dev_sectors;
	if (test_bit(MD_RECOVERY_SYNC, &mddev->recovery) ||
	    test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery))
		max_sector = mddev->resync_max_sectors;
	if (sector_nr >= max_sector) {
		/* If we aborted, we need to abort the
		 * sync on the 'current' bitmap chucks (there can
		 * be several when recovering multiple devices).
		 * as we may have started syncing it but not finished.
		 * We can find the current address in
		 * mddev->curr_resync, but for recovery,
		 * we need to convert that to several
		 * virtual addresses.
		 */
		if (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery)) {
			end_reshape(conf);
			close_sync(conf);
			return 0;
		}

		if (mddev->curr_resync < max_sector) { /* aborted */
			if (test_bit(MD_RECOVERY_SYNC, &mddev->recovery))
				bitmap_end_sync(mddev->bitmap, mddev->curr_resync,
						&sync_blocks, 1);
			else for (i = 0; i < conf->geo.raid_disks; i++) {
				sector_t sect =
					raid10_find_virt(conf, mddev->curr_resync, i);
				bitmap_end_sync(mddev->bitmap, sect,
						&sync_blocks, 1);
			}
		} else {
			/* completed sync */
			if ((!mddev->bitmap || conf->fullsync)
			    && conf->have_replacement
			    && test_bit(MD_RECOVERY_SYNC, &mddev->recovery)) {
				/* Completed a full sync so the replacements
				 * are now fully recovered.
				 */
				for (i = 0; i < conf->geo.raid_disks; i++)
					if (conf->mirrors[i].replacement)
						conf->mirrors[i].replacement
							->recovery_offset
							= MaxSector;
			}
			conf->fullsync = 0;
		}
		bitmap_close_sync(mddev->bitmap);
		close_sync(conf);
		*skipped = 1;
		return sectors_skipped;
	}

	if (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery))
		return reshape_request(mddev, sector_nr, skipped);

	if (chunks_skipped >= conf->geo.raid_disks) {
		/* if there has been nothing to do on any drive,
		 * then there is nothing to do at all..
		 */
		*skipped = 1;
		return (max_sector - sector_nr) + sectors_skipped;
	}

	if (max_sector > mddev->resync_max)
		max_sector = mddev->resync_max; /* Don't do IO beyond here */

	/* make sure whole request will fit in a chunk - if chunks
	 * are meaningful
	 */
	if (conf->geo.near_copies < conf->geo.raid_disks &&
	    max_sector > (sector_nr | chunk_mask))
		max_sector = (sector_nr | chunk_mask) + 1;
	/*
	 * If there is non-resync activity waiting for us then
	 * put in a delay to throttle resync.
	 */
	if (!go_faster && conf->nr_waiting)
		msleep_interruptible(1000);

	/* Again, very different code for resync and recovery.
	 * Both must result in an r10bio with a list of bios that
	 * have bi_end_io, bi_sector, bi_bdev set,
	 * and bi_private set to the r10bio.
	 * For recovery, we may actually create several r10bios
	 * with 2 bios in each, that correspond to the bios in the main one.
	 * In this case, the subordinate r10bios link back through a
	 * borrowed master_bio pointer, and the counter in the master
	 * includes a ref from each subordinate.
	 */
	/* First, we decide what to do and set ->bi_end_io
	 * To end_sync_read if we want to read, and
	 * end_sync_write if we will want to write.
	 */

	max_sync = RESYNC_PAGES << (PAGE_SHIFT-9);
	if (!test_bit(MD_RECOVERY_SYNC, &mddev->recovery)) {
		/* recovery... the complicated one */
		int j;
		r10_bio = NULL;

		for (i = 0 ; i < conf->geo.raid_disks; i++) {
			int still_degraded;
			struct r10bio *rb2;
			sector_t sect;
			int must_sync;
			int any_working;
			struct raid10_info *mirror = &conf->mirrors[i];

			if ((mirror->rdev == NULL ||
			     test_bit(In_sync, &mirror->rdev->flags))
			    &&
			    (mirror->replacement == NULL ||
			     test_bit(Faulty,
				      &mirror->replacement->flags)))
				continue;

			still_degraded = 0;
			/* want to reconstruct this device */
			rb2 = r10_bio;
			sect = raid10_find_virt(conf, sector_nr, i);
			if (sect >= mddev->resync_max_sectors) {
				/* last stripe is not complete - don't
				 * try to recover this sector.
				 */
				continue;
			}
			/* Unless we are doing a full sync, or a replacement
			 * we only need to recover the block if it is set in
			 * the bitmap
			 */
			must_sync = bitmap_start_sync(mddev->bitmap, sect,
						      &sync_blocks, 1);
			if (sync_blocks < max_sync)
				max_sync = sync_blocks;
			if (!must_sync &&
			    mirror->replacement == NULL &&
			    !conf->fullsync) {
				/* yep, skip the sync_blocks here, but don't assume
				 * that there will never be anything to do here
				 */
				chunks_skipped = -1;
				continue;
			}

			r10_bio = mempool_alloc(conf->r10buf_pool, GFP_NOIO);
			r10_bio->state = 0;
			raise_barrier(conf, rb2 != NULL);
			atomic_set(&r10_bio->remaining, 0);

			r10_bio->master_bio = (struct bio*)rb2;
			if (rb2)
				atomic_inc(&rb2->remaining);
			r10_bio->mddev = mddev;
			set_bit(R10BIO_IsRecover, &r10_bio->state);
			r10_bio->sector = sect;

			raid10_find_phys(conf, r10_bio);

			/* Need to check if the array will still be
			 * degraded
			 */
			for (j = 0; j < conf->geo.raid_disks; j++)
				if (conf->mirrors[j].rdev == NULL ||
				    test_bit(Faulty, &conf->mirrors[j].rdev->flags)) {
					still_degraded = 1;
					break;
				}

			must_sync = bitmap_start_sync(mddev->bitmap, sect,
						      &sync_blocks, still_degraded);

			any_working = 0;
			for (j=0; j<conf->copies;j++) {
				int k;
				int d = r10_bio->devs[j].devnum;
				sector_t from_addr, to_addr;
				struct md_rdev *rdev;
				sector_t sector, first_bad;
				int bad_sectors;
				if (!conf->mirrors[d].rdev ||
				    !test_bit(In_sync, &conf->mirrors[d].rdev->flags))
					continue;
				/* This is where we read from */
				any_working = 1;
				rdev = conf->mirrors[d].rdev;
				sector = r10_bio->devs[j].addr;

				if (is_badblock(rdev, sector, max_sync,
						&first_bad, &bad_sectors)) {
					if (first_bad > sector)
						max_sync = first_bad - sector;
					else {
						bad_sectors -= (sector
								- first_bad);
						if (max_sync > bad_sectors)
							max_sync = bad_sectors;
						continue;
					}
				}
				bio = r10_bio->devs[0].bio;
				bio_reset(bio);
				bio->bi_next = biolist;
				biolist = bio;
				bio->bi_private = r10_bio;
				bio->bi_end_io = end_sync_read;
				bio->bi_rw = READ;
				from_addr = r10_bio->devs[j].addr;
				bio->bi_iter.bi_sector = from_addr +
					rdev->data_offset;
				bio->bi_bdev = rdev->bdev;
				atomic_inc(&rdev->nr_pending);
				/* and we write to 'i' (if not in_sync) */

				for (k=0; k<conf->copies; k++)
					if (r10_bio->devs[k].devnum == i)
						break;
				BUG_ON(k == conf->copies);
				to_addr = r10_bio->devs[k].addr;
				r10_bio->devs[0].devnum = d;
				r10_bio->devs[0].addr = from_addr;
				r10_bio->devs[1].devnum = i;
				r10_bio->devs[1].addr = to_addr;

				rdev = mirror->rdev;
				if (!test_bit(In_sync, &rdev->flags)) {
					bio = r10_bio->devs[1].bio;
					bio_reset(bio);
					bio->bi_next = biolist;
					biolist = bio;
					bio->bi_private = r10_bio;
					bio->bi_end_io = end_sync_write;
					bio->bi_rw = WRITE;
					bio->bi_iter.bi_sector = to_addr
						+ rdev->data_offset;
					bio->bi_bdev = rdev->bdev;
					atomic_inc(&r10_bio->remaining);
				} else
					r10_bio->devs[1].bio->bi_end_io = NULL;

				/* and maybe write to replacement */
				bio = r10_bio->devs[1].repl_bio;
				if (bio)
					bio->bi_end_io = NULL;
				rdev = mirror->replacement;
				/* Note: if rdev != NULL, then bio
				 * cannot be NULL as r10buf_pool_alloc will
				 * have allocated it.
				 * So the second test here is pointless.
				 * But it keeps semantic-checkers happy, and
				 * this comment keeps human reviewers
				 * happy.
				 */
				if (rdev == NULL || bio == NULL ||
				    test_bit(Faulty, &rdev->flags))
					break;
				bio_reset(bio);
				bio->bi_next = biolist;
				biolist = bio;
				bio->bi_private = r10_bio;
				bio->bi_end_io = end_sync_write;
				bio->bi_rw = WRITE;
				bio->bi_iter.bi_sector = to_addr +
					rdev->data_offset;
				bio->bi_bdev = rdev->bdev;
				atomic_inc(&r10_bio->remaining);
				break;
			}
			if (j == conf->copies) {
				/* Cannot recover, so abort the recovery or
				 * record a bad block */
				if (any_working) {
					/* problem is that there are bad blocks
					 * on other device(s)
					 */
					int k;
					for (k = 0; k < conf->copies; k++)
						if (r10_bio->devs[k].devnum == i)
							break;
					if (!test_bit(In_sync,
						      &mirror->rdev->flags)
					    && !rdev_set_badblocks(
						    mirror->rdev,
						    r10_bio->devs[k].addr,
						    max_sync, 0))
						any_working = 0;
					if (mirror->replacement &&
					    !rdev_set_badblocks(
						    mirror->replacement,
						    r10_bio->devs[k].addr,
						    max_sync, 0))
						any_working = 0;
				}
				if (!any_working)  {
					if (!test_and_set_bit(MD_RECOVERY_INTR,
							      &mddev->recovery))
						printk(KERN_INFO "md/raid10:%s: insufficient "
						       "working devices for recovery.\n",
						       mdname(mddev));
					mirror->recovery_disabled
						= mddev->recovery_disabled;
				}
				put_buf(r10_bio);
				if (rb2)
					atomic_dec(&rb2->remaining);
				r10_bio = rb2;
				break;
			}
		}
		if (biolist == NULL) {
			while (r10_bio) {
				struct r10bio *rb2 = r10_bio;
				r10_bio = (struct r10bio*) rb2->master_bio;
				rb2->master_bio = NULL;
				put_buf(rb2);
			}
			goto giveup;
		}
	} else {
		/* resync. Schedule a read for every block at this virt offset */
		int count = 0;

		bitmap_cond_end_sync(mddev->bitmap, sector_nr);

		if (!bitmap_start_sync(mddev->bitmap, sector_nr,
				       &sync_blocks, mddev->degraded) &&
		    !conf->fullsync && !test_bit(MD_RECOVERY_REQUESTED,
						 &mddev->recovery)) {
			/* We can skip this block */
			*skipped = 1;
			return sync_blocks + sectors_skipped;
		}
		if (sync_blocks < max_sync)
			max_sync = sync_blocks;
		r10_bio = mempool_alloc(conf->r10buf_pool, GFP_NOIO);
		r10_bio->state = 0;

		r10_bio->mddev = mddev;
		atomic_set(&r10_bio->remaining, 0);
		raise_barrier(conf, 0);
		conf->next_resync = sector_nr;

		r10_bio->master_bio = NULL;
		r10_bio->sector = sector_nr;
		set_bit(R10BIO_IsSync, &r10_bio->state);
		raid10_find_phys(conf, r10_bio);
		r10_bio->sectors = (sector_nr | chunk_mask) - sector_nr + 1;

		for (i = 0; i < conf->copies; i++) {
			int d = r10_bio->devs[i].devnum;
			sector_t first_bad, sector;
			int bad_sectors;

			if (r10_bio->devs[i].repl_bio)
				r10_bio->devs[i].repl_bio->bi_end_io = NULL;

			bio = r10_bio->devs[i].bio;
			bio_reset(bio);
			clear_bit(BIO_UPTODATE, &bio->bi_flags);
			if (conf->mirrors[d].rdev == NULL ||
			    test_bit(Faulty, &conf->mirrors[d].rdev->flags))
				continue;
			sector = r10_bio->devs[i].addr;
			if (is_badblock(conf->mirrors[d].rdev,
					sector, max_sync,
					&first_bad, &bad_sectors)) {
				if (first_bad > sector)
					max_sync = first_bad - sector;
				else {
					bad_sectors -= (sector - first_bad);
					if (max_sync > bad_sectors)
						max_sync = bad_sectors;
					continue;
				}
			}
			atomic_inc(&conf->mirrors[d].rdev->nr_pending);
			atomic_inc(&r10_bio->remaining);
			bio->bi_next = biolist;
			biolist = bio;
			bio->bi_private = r10_bio;
			bio->bi_end_io = end_sync_read;
			bio->bi_rw = READ;
			bio->bi_iter.bi_sector = sector +
				conf->mirrors[d].rdev->data_offset;
			bio->bi_bdev = conf->mirrors[d].rdev->bdev;
			count++;

			if (conf->mirrors[d].replacement == NULL ||
			    test_bit(Faulty,
				     &conf->mirrors[d].replacement->flags))
				continue;

			/* Need to set up for writing to the replacement */
			bio = r10_bio->devs[i].repl_bio;
			bio_reset(bio);
			clear_bit(BIO_UPTODATE, &bio->bi_flags);

			sector = r10_bio->devs[i].addr;
			atomic_inc(&conf->mirrors[d].rdev->nr_pending);
			bio->bi_next = biolist;
			biolist = bio;
			bio->bi_private = r10_bio;
			bio->bi_end_io = end_sync_write;
			bio->bi_rw = WRITE;
			bio->bi_iter.bi_sector = sector +
				conf->mirrors[d].replacement->data_offset;
			bio->bi_bdev = conf->mirrors[d].replacement->bdev;
			count++;
		}

		if (count < 2) {
			for (i=0; i<conf->copies; i++) {
				int d = r10_bio->devs[i].devnum;
				if (r10_bio->devs[i].bio->bi_end_io)
					rdev_dec_pending(conf->mirrors[d].rdev,
							 mddev);
				if (r10_bio->devs[i].repl_bio &&
				    r10_bio->devs[i].repl_bio->bi_end_io)
					rdev_dec_pending(
						conf->mirrors[d].replacement,
						mddev);
			}
			put_buf(r10_bio);
			biolist = NULL;
			goto giveup;
		}
	}

	nr_sectors = 0;
	if (sector_nr + max_sync < max_sector)
		max_sector = sector_nr + max_sync;
	do {
		struct page *page;
		int len = PAGE_SIZE;
		if (sector_nr + (len>>9) > max_sector)
			len = (max_sector - sector_nr) << 9;
		if (len == 0)
			break;
		for (bio= biolist ; bio ; bio=bio->bi_next) {
			struct bio *bio2;
			page = bio->bi_io_vec[bio->bi_vcnt].bv_page;
			if (bio_add_page(bio, page, len, 0))
				continue;

			/* stop here */
			bio->bi_io_vec[bio->bi_vcnt].bv_page = page;
			for (bio2 = biolist;
			     bio2 && bio2 != bio;
			     bio2 = bio2->bi_next) {
				/* remove last page from this bio */
				bio2->bi_vcnt--;
				bio2->bi_iter.bi_size -= len;
				__clear_bit(BIO_SEG_VALID, &bio2->bi_flags);
			}
			goto bio_full;
		}
		nr_sectors += len>>9;
		sector_nr += len>>9;
	} while (biolist->bi_vcnt < RESYNC_PAGES);
 bio_full:
	r10_bio->sectors = nr_sectors;

	while (biolist) {
		bio = biolist;
		biolist = biolist->bi_next;

		bio->bi_next = NULL;
		r10_bio = bio->bi_private;
		r10_bio->sectors = nr_sectors;

		if (bio->bi_end_io == end_sync_read) {
			md_sync_acct(bio->bi_bdev, nr_sectors);
			set_bit(BIO_UPTODATE, &bio->bi_flags);
			generic_make_request(bio);
		}
	}

	if (sectors_skipped)
		/* pretend they weren't skipped, it makes
		 * no important difference in this case
		 */
		md_done_sync(mddev, sectors_skipped, 1);

	return sectors_skipped + nr_sectors;
 giveup:
	/* There is nowhere to write, so all non-sync
	 * drives must be failed or in resync, all drives
	 * have a bad block, so try the next chunk...
	 */
	if (sector_nr + max_sync < max_sector)
		max_sector = sector_nr + max_sync;

	sectors_skipped += (max_sector - sector_nr);
	chunks_skipped ++;
	sector_nr = max_sector;
	goto skipped;
}

static sector_t
raid10_size(struct mddev *mddev, sector_t sectors, int raid_disks)
{
	sector_t size;
	struct r10conf *conf = mddev->private;

	if (!raid_disks)
		raid_disks = min(conf->geo.raid_disks,
				 conf->prev.raid_disks);
	if (!sectors)
		sectors = conf->dev_sectors;

	size = sectors >> conf->geo.chunk_shift;
	sector_div(size, conf->geo.far_copies);
	size = size * raid_disks;
	sector_div(size, conf->geo.near_copies);

	return size << conf->geo.chunk_shift;
}

static void calc_sectors(struct r10conf *conf, sector_t size)
{
	/* Calculate the number of sectors-per-device that will
	 * actually be used, and set conf->dev_sectors and
	 * conf->stride
	 */

	size = size >> conf->geo.chunk_shift;
	sector_div(size, conf->geo.far_copies);
	size = size * conf->geo.raid_disks;
	sector_div(size, conf->geo.near_copies);
	/* 'size' is now the number of chunks in the array */
	/* calculate "used chunks per device" */
	size = size * conf->copies;

	/* We need to round up when dividing by raid_disks to
	 * get the stride size.
	 */
	size = DIV_ROUND_UP_SECTOR_T(size, conf->geo.raid_disks);

	conf->dev_sectors = size << conf->geo.chunk_shift;

	if (conf->geo.far_offset)
		conf->geo.stride = 1 << conf->geo.chunk_shift;
	else {
		sector_div(size, conf->geo.far_copies);
		conf->geo.stride = size << conf->geo.chunk_shift;
	}
}

enum geo_type {geo_new, geo_old, geo_start};
static int setup_geo(struct geom *geo, struct mddev *mddev, enum geo_type new)
{
	int nc, fc, fo;
	int layout, chunk, disks;
	switch (new) {
	case geo_old:
		layout = mddev->layout;
		chunk = mddev->chunk_sectors;
		disks = mddev->raid_disks - mddev->delta_disks;
		break;
	case geo_new:
		layout = mddev->new_layout;
		chunk = mddev->new_chunk_sectors;
		disks = mddev->raid_disks;
		break;
	default: /* avoid 'may be unused' warnings */
	case geo_start: /* new when starting reshape - raid_disks not
			 * updated yet. */
		layout = mddev->new_layout;
		chunk = mddev->new_chunk_sectors;
		disks = mddev->raid_disks + mddev->delta_disks;
		break;
	}
	if (layout >> 18)
		return -1;
	if (chunk < (PAGE_SIZE >> 9) ||
	    !is_power_of_2(chunk))
		return -2;
	nc = layout & 255;
	fc = (layout >> 8) & 255;
	fo = layout & (1<<16);
	geo->raid_disks = disks;
	geo->near_copies = nc;
	geo->far_copies = fc;
	geo->far_offset = fo;
	geo->far_set_size = (layout & (1<<17)) ? disks / fc : disks;
	geo->chunk_mask = chunk - 1;
	geo->chunk_shift = ffz(~chunk);
	return nc*fc;
}

static struct r10conf *setup_conf(struct mddev *mddev)
{
	struct r10conf *conf = NULL;
	int err = -EINVAL;
	struct geom geo;
	int copies;

	copies = setup_geo(&geo, mddev, geo_new);

	if (copies == -2) {
		printk(KERN_ERR "md/raid10:%s: chunk size must be "
		       "at least PAGE_SIZE(%ld) and be a power of 2.\n",
		       mdname(mddev), PAGE_SIZE);
		goto out;
	}

	if (copies < 2 || copies > mddev->raid_disks) {
		printk(KERN_ERR "md/raid10:%s: unsupported raid10 layout: 0x%8x\n",
		       mdname(mddev), mddev->new_layout);
		goto out;
	}

	err = -ENOMEM;
	conf = kzalloc(sizeof(struct r10conf), GFP_KERNEL);
	if (!conf)
		goto out;

	/* FIXME calc properly */
	conf->mirrors = kzalloc(sizeof(struct raid10_info)*(mddev->raid_disks +
							    max(0,-mddev->delta_disks)),
				GFP_KERNEL);
	if (!conf->mirrors)
		goto out;

	conf->tmppage = alloc_page(GFP_KERNEL);
	if (!conf->tmppage)
		goto out;

	conf->geo = geo;
	conf->copies = copies;
	conf->r10bio_pool = mempool_create(NR_RAID10_BIOS, r10bio_pool_alloc,
					   r10bio_pool_free, conf);
	if (!conf->r10bio_pool)
		goto out;

	calc_sectors(conf, mddev->dev_sectors);
	if (mddev->reshape_position == MaxSector) {
		conf->prev = conf->geo;
		conf->reshape_progress = MaxSector;
	} else {
		if (setup_geo(&conf->prev, mddev, geo_old) != conf->copies) {
			err = -EINVAL;
			goto out;
		}
		conf->reshape_progress = mddev->reshape_position;
		if (conf->prev.far_offset)
			conf->prev.stride = 1 << conf->prev.chunk_shift;
		else
			/* far_copies must be 1 */
			conf->prev.stride = conf->dev_sectors;
	}
	spin_lock_init(&conf->device_lock);
	INIT_LIST_HEAD(&conf->retry_list);

	spin_lock_init(&conf->resync_lock);
	init_waitqueue_head(&conf->wait_barrier);

	conf->thread = md_register_thread(raid10d, mddev, "raid10");
	if (!conf->thread)
		goto out;

	conf->mddev = mddev;
	return conf;

 out:
	if (err == -ENOMEM)
		printk(KERN_ERR "md/raid10:%s: couldn't allocate memory.\n",
		       mdname(mddev));
	if (conf) {
		if (conf->r10bio_pool)
			mempool_destroy(conf->r10bio_pool);
		kfree(conf->mirrors);
		safe_put_page(conf->tmppage);
		kfree(conf);
	}
	return ERR_PTR(err);
}

static int run(struct mddev *mddev)
{
	struct r10conf *conf;
	int i, disk_idx, chunk_size;
	struct raid10_info *disk;
	struct md_rdev *rdev;
	sector_t size;
	sector_t min_offset_diff = 0;
	int first = 1;
	bool discard_supported = false;

	if (mddev->private == NULL) {
		conf = setup_conf(mddev);
		if (IS_ERR(conf))
			return PTR_ERR(conf);
		mddev->private = conf;
	}
	conf = mddev->private;
	if (!conf)
		goto out;

	mddev->thread = conf->thread;
	conf->thread = NULL;

	chunk_size = mddev->chunk_sectors << 9;
	if (mddev->queue) {
		blk_queue_max_discard_sectors(mddev->queue,
					      mddev->chunk_sectors);
		blk_queue_max_write_same_sectors(mddev->queue, 0);
		blk_queue_io_min(mddev->queue, chunk_size);
		if (conf->geo.raid_disks % conf->geo.near_copies)
			blk_queue_io_opt(mddev->queue, chunk_size * conf->geo.raid_disks);
		else
			blk_queue_io_opt(mddev->queue, chunk_size *
					 (conf->geo.raid_disks / conf->geo.near_copies));
	}

	rdev_for_each(rdev, mddev) {
		long long diff;
		struct request_queue *q;

		disk_idx = rdev->raid_disk;
		if (disk_idx < 0)
			continue;
		if (disk_idx >= conf->geo.raid_disks &&
		    disk_idx >= conf->prev.raid_disks)
			continue;
		disk = conf->mirrors + disk_idx;

		if (test_bit(Replacement, &rdev->flags)) {
			if (disk->replacement)
				goto out_free_conf;
			disk->replacement = rdev;
		} else {
			if (disk->rdev)
				goto out_free_conf;
			disk->rdev = rdev;
		}
		q = bdev_get_queue(rdev->bdev);
		if (q->merge_bvec_fn)
			mddev->merge_check_needed = 1;
		diff = (rdev->new_data_offset - rdev->data_offset);
		if (!mddev->reshape_backwards)
			diff = -diff;
		if (diff < 0)
			diff = 0;
		if (first || diff < min_offset_diff)
			min_offset_diff = diff;

		if (mddev->gendisk)
			disk_stack_limits(mddev->gendisk, rdev->bdev,
					  rdev->data_offset << 9);

		disk->head_position = 0;

		if (blk_queue_discard(bdev_get_queue(rdev->bdev)))
			discard_supported = true;
	}

	if (mddev->queue) {
		if (discard_supported)
			queue_flag_set_unlocked(QUEUE_FLAG_DISCARD,
						mddev->queue);
		else
			queue_flag_clear_unlocked(QUEUE_FLAG_DISCARD,
						  mddev->queue);
	}
	/* need to check that every block has at least one working mirror */
	if (!enough(conf, -1)) {
		printk(KERN_ERR "md/raid10:%s: not enough operational mirrors.\n",
		       mdname(mddev));
		goto out_free_conf;
	}

	if (conf->reshape_progress != MaxSector) {
		/* must ensure that shape change is supported */
		if (conf->geo.far_copies != 1 &&
		    conf->geo.far_offset == 0)
			goto out_free_conf;
		if (conf->prev.far_copies != 1 &&
		    conf->prev.far_offset == 0)
			goto out_free_conf;
	}

	mddev->degraded = 0;
	for (i = 0;
	     i < conf->geo.raid_disks
		     || i < conf->prev.raid_disks;
	     i++) {

		disk = conf->mirrors + i;

		if (!disk->rdev && disk->replacement) {
			/* The replacement is all we have - use it */
			disk->rdev = disk->replacement;
			disk->replacement = NULL;
			clear_bit(Replacement, &disk->rdev->flags);
		}

		if (!disk->rdev ||
		    !test_bit(In_sync, &disk->rdev->flags)) {
			disk->head_position = 0;
			mddev->degraded++;
			if (disk->rdev &&
			    disk->rdev->saved_raid_disk < 0)
				conf->fullsync = 1;
		}
		disk->recovery_disabled = mddev->recovery_disabled - 1;
	}

	if (mddev->recovery_cp != MaxSector)
		printk(KERN_NOTICE "md/raid10:%s: not clean"
		       " -- starting background reconstruction\n",
		       mdname(mddev));
	printk(KERN_INFO
		"md/raid10:%s: active with %d out of %d devices\n",
		mdname(mddev), conf->geo.raid_disks - mddev->degraded,
		conf->geo.raid_disks);
	/*
	 * Ok, everything is just fine now
	 */
	mddev->dev_sectors = conf->dev_sectors;
	size = raid10_size(mddev, 0, 0);
	md_set_array_sectors(mddev, size);
	mddev->resync_max_sectors = size;

	if (mddev->queue) {
		int stripe = conf->geo.raid_disks *
			((mddev->chunk_sectors << 9) / PAGE_SIZE);
		mddev->queue->backing_dev_info.congested_fn = raid10_congested;
		mddev->queue->backing_dev_info.congested_data = mddev;

		/* Calculate max read-ahead size.
		 * We need to readahead at least twice a whole stripe....
		 * maybe...
		 */
		stripe /= conf->geo.near_copies;
		if (mddev->queue->backing_dev_info.ra_pages < 2 * stripe)
			mddev->queue->backing_dev_info.ra_pages = 2 * stripe;
		blk_queue_merge_bvec(mddev->queue, raid10_mergeable_bvec);
	}

	if (md_integrity_register(mddev))
		goto out_free_conf;

	if (conf->reshape_progress != MaxSector) {
		unsigned long before_length, after_length;

		before_length = ((1 << conf->prev.chunk_shift) *
				 conf->prev.far_copies);
		after_length = ((1 << conf->geo.chunk_shift) *
				conf->geo.far_copies);

		if (max(before_length, after_length) > min_offset_diff) {
			/* This cannot work */
			printk("md/raid10: offset difference not enough to continue reshape\n");
			goto out_free_conf;
		}
		conf->offset_diff = min_offset_diff;

		conf->reshape_safe = conf->reshape_progress;
		clear_bit(MD_RECOVERY_SYNC, &mddev->recovery);
		clear_bit(MD_RECOVERY_CHECK, &mddev->recovery);
		set_bit(MD_RECOVERY_RESHAPE, &mddev->recovery);
		set_bit(MD_RECOVERY_RUNNING, &mddev->recovery);
		mddev->sync_thread = md_register_thread(md_do_sync, mddev,
							"reshape");
	}

	return 0;

out_free_conf:
	md_unregister_thread(&mddev->thread);
	if (conf->r10bio_pool)
		mempool_destroy(conf->r10bio_pool);
	safe_put_page(conf->tmppage);
	kfree(conf->mirrors);
	kfree(conf);
	mddev->private = NULL;
out:
	return -EIO;
}

static int stop(struct mddev *mddev)
{
	struct r10conf *conf = mddev->private;

	raise_barrier(conf, 0);
	lower_barrier(conf);

	md_unregister_thread(&mddev->thread);
	if (mddev->queue)
		/* the unplug fn references 'conf'*/
		blk_sync_queue(mddev->queue);

	if (conf->r10bio_pool)
		mempool_destroy(conf->r10bio_pool);
	safe_put_page(conf->tmppage);
	kfree(conf->mirrors);
	kfree(conf->mirrors_old);
	kfree(conf->mirrors_new);
	kfree(conf);
	mddev->private = NULL;
	return 0;
}

static void raid10_quiesce(struct mddev *mddev, int state)
{
	struct r10conf *conf = mddev->private;

	switch(state) {
	case 1:
		raise_barrier(conf, 0);
		break;
	case 0:
		lower_barrier(conf);
		break;
	}
}

static int raid10_resize(struct mddev *mddev, sector_t sectors)
{
	/* Resize of 'far' arrays is not supported.
	 * For 'near' and 'offset' arrays we can set the
	 * number of sectors used to be an appropriate multiple
	 * of the chunk size.
	 * For 'offset', this is far_copies*chunksize.
	 * For 'near' the multiplier is the LCM of
	 * near_copies and raid_disks.
	 * So if far_copies > 1 && !far_offset, fail.
	 * Else find LCM(raid_disks, near_copy)*far_copies and
	 * multiply by chunk_size.  Then round to this number.
	 * This is mostly done by raid10_size()
	 */
	struct r10conf *conf = mddev->private;
	sector_t oldsize, size;

	if (mddev->reshape_position != MaxSector)
		return -EBUSY;

	if (conf->geo.far_copies > 1 && !conf->geo.far_offset)
		return -EINVAL;

	oldsize = raid10_size(mddev, 0, 0);
	size = raid10_size(mddev, sectors, 0);
	if (mddev->external_size &&
	    mddev->array_sectors > size)
		return -EINVAL;
	if (mddev->bitmap) {
		int ret = bitmap_resize(mddev->bitmap, size, 0, 0);
		if (ret)
			return ret;
	}
	md_set_array_sectors(mddev, size);
	set_capacity(mddev->gendisk, mddev->array_sectors);
	revalidate_disk(mddev->gendisk);
	if (sectors > mddev->dev_sectors &&
	    mddev->recovery_cp > oldsize) {
		mddev->recovery_cp = oldsize;
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	}
	calc_sectors(conf, sectors);
	mddev->dev_sectors = conf->dev_sectors;
	mddev->resync_max_sectors = size;
	return 0;
}

static void *raid10_takeover_raid0(struct mddev *mddev)
{
	struct md_rdev *rdev;
	struct r10conf *conf;

	if (mddev->degraded > 0) {
		printk(KERN_ERR "md/raid10:%s: Error: degraded raid0!\n",
		       mdname(mddev));
		return ERR_PTR(-EINVAL);
	}

	/* Set new parameters */
	mddev->new_level = 10;
	/* new layout: far_copies = 1, near_copies = 2 */
	mddev->new_layout = (1<<8) + 2;
	mddev->new_chunk_sectors = mddev->chunk_sectors;
	mddev->delta_disks = mddev->raid_disks;
	mddev->raid_disks *= 2;
	/* make sure it will be not marked as dirty */
	mddev->recovery_cp = MaxSector;

	conf = setup_conf(mddev);
	if (!IS_ERR(conf)) {
		rdev_for_each(rdev, mddev)
			if (rdev->raid_disk >= 0)
				rdev->new_raid_disk = rdev->raid_disk * 2;
		conf->barrier = 1;
	}

	return conf;
}

static void *raid10_takeover(struct mddev *mddev)
{
	struct r0conf *raid0_conf;

	/* raid10 can take over:
	 *  raid0 - providing it has only two drives
	 */
	if (mddev->level == 0) {
		/* for raid0 takeover only one zone is supported */
		raid0_conf = mddev->private;
		if (raid0_conf->nr_strip_zones > 1) {
			printk(KERN_ERR "md/raid10:%s: cannot takeover raid 0"
			       " with more than one zone.\n",
			       mdname(mddev));
			return ERR_PTR(-EINVAL);
		}
		return raid10_takeover_raid0(mddev);
	}
	return ERR_PTR(-EINVAL);
}

static int raid10_check_reshape(struct mddev *mddev)
{
	/* Called when there is a request to change
	 * - layout (to ->new_layout)
	 * - chunk size (to ->new_chunk_sectors)
	 * - raid_disks (by delta_disks)
	 * or when trying to restart a reshape that was ongoing.
	 *
	 * We need to validate the request and possibly allocate
	 * space if that might be an issue later.
	 *
	 * Currently we reject any reshape of a 'far' mode array,
	 * allow chunk size to change if new is generally acceptable,
	 * allow raid_disks to increase, and allow
	 * a switch between 'near' mode and 'offset' mode.
	 */
	struct r10conf *conf = mddev->private;
	struct geom geo;

	if (conf->geo.far_copies != 1 && !conf->geo.far_offset)
		return -EINVAL;

	if (setup_geo(&geo, mddev, geo_start) != conf->copies)
		/* mustn't change number of copies */
		return -EINVAL;
	if (geo.far_copies > 1 && !geo.far_offset)
		/* Cannot switch to 'far' mode */
		return -EINVAL;

	if (mddev->array_sectors & geo.chunk_mask)
			/* not factor of array size */
			return -EINVAL;

	if (!enough(conf, -1))
		return -EINVAL;

	kfree(conf->mirrors_new);
	conf->mirrors_new = NULL;
	if (mddev->delta_disks > 0) {
		/* allocate new 'mirrors' list */
		conf->mirrors_new = kzalloc(
			sizeof(struct raid10_info)
			*(mddev->raid_disks +
			  mddev->delta_disks),
			GFP_KERNEL);
		if (!conf->mirrors_new)
			return -ENOMEM;
	}
	return 0;
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
static int calc_degraded(struct r10conf *conf)
{
	int degraded, degraded2;
	int i;

	rcu_read_lock();
	degraded = 0;
	/* 'prev' section first */
	for (i = 0; i < conf->prev.raid_disks; i++) {
		struct md_rdev *rdev = rcu_dereference(conf->mirrors[i].rdev);
		if (!rdev || test_bit(Faulty, &rdev->flags))
			degraded++;
		else if (!test_bit(In_sync, &rdev->flags))
			/* When we can reduce the number of devices in
			 * an array, this might not contribute to
			 * 'degraded'.  It does now.
			 */
			degraded++;
	}
	rcu_read_unlock();
	if (conf->geo.raid_disks == conf->prev.raid_disks)
		return degraded;
	rcu_read_lock();
	degraded2 = 0;
	for (i = 0; i < conf->geo.raid_disks; i++) {
		struct md_rdev *rdev = rcu_dereference(conf->mirrors[i].rdev);
		if (!rdev || test_bit(Faulty, &rdev->flags))
			degraded2++;
		else if (!test_bit(In_sync, &rdev->flags)) {
			/* If reshape is increasing the number of devices,
			 * this section has already been recovered, so
			 * it doesn't contribute to degraded.
			 * else it does.
			 */
			if (conf->geo.raid_disks <= conf->prev.raid_disks)
				degraded2++;
		}
	}
	rcu_read_unlock();
	if (degraded2 > degraded)
		return degraded2;
	return degraded;
}

static int raid10_start_reshape(struct mddev *mddev)
{
	/* A 'reshape' has been requested. This commits
	 * the various 'new' fields and sets MD_RECOVER_RESHAPE
	 * This also checks if there are enough spares and adds them
	 * to the array.
	 * We currently require enough spares to make the final
	 * array non-degraded.  We also require that the difference
	 * between old and new data_offset - on each device - is
	 * enough that we never risk over-writing.
	 */

	unsigned long before_length, after_length;
	sector_t min_offset_diff = 0;
	int first = 1;
	struct geom new;
	struct r10conf *conf = mddev->private;
	struct md_rdev *rdev;
	int spares = 0;
	int ret;

	if (test_bit(MD_RECOVERY_RUNNING, &mddev->recovery))
		return -EBUSY;

	if (setup_geo(&new, mddev, geo_start) != conf->copies)
		return -EINVAL;

	before_length = ((1 << conf->prev.chunk_shift) *
			 conf->prev.far_copies);
	after_length = ((1 << conf->geo.chunk_shift) *
			conf->geo.far_copies);

	rdev_for_each(rdev, mddev) {
		if (!test_bit(In_sync, &rdev->flags)
		    && !test_bit(Faulty, &rdev->flags))
			spares++;
		if (rdev->raid_disk >= 0) {
			long long diff = (rdev->new_data_offset
					  - rdev->data_offset);
			if (!mddev->reshape_backwards)
				diff = -diff;
			if (diff < 0)
				diff = 0;
			if (first || diff < min_offset_diff)
				min_offset_diff = diff;
		}
	}

	if (max(before_length, after_length) > min_offset_diff)
		return -EINVAL;

	if (spares < mddev->delta_disks)
		return -EINVAL;

	conf->offset_diff = min_offset_diff;
	spin_lock_irq(&conf->device_lock);
	if (conf->mirrors_new) {
		memcpy(conf->mirrors_new, conf->mirrors,
		       sizeof(struct raid10_info)*conf->prev.raid_disks);
		smp_mb();
		kfree(conf->mirrors_old);
		conf->mirrors_old = conf->mirrors;
		conf->mirrors = conf->mirrors_new;
		conf->mirrors_new = NULL;
	}
	setup_geo(&conf->geo, mddev, geo_start);
	smp_mb();
	if (mddev->reshape_backwards) {
		sector_t size = raid10_size(mddev, 0, 0);
		if (size < mddev->array_sectors) {
			spin_unlock_irq(&conf->device_lock);
			printk(KERN_ERR "md/raid10:%s: array size must be reduce before number of disks\n",
			       mdname(mddev));
			return -EINVAL;
		}
		mddev->resync_max_sectors = size;
		conf->reshape_progress = size;
	} else
		conf->reshape_progress = 0;
	spin_unlock_irq(&conf->device_lock);

	if (mddev->delta_disks && mddev->bitmap) {
		ret = bitmap_resize(mddev->bitmap,
				    raid10_size(mddev, 0,
						conf->geo.raid_disks),
				    0, 0);
		if (ret)
			goto abort;
	}
	if (mddev->delta_disks > 0) {
		rdev_for_each(rdev, mddev)
			if (rdev->raid_disk < 0 &&
			    !test_bit(Faulty, &rdev->flags)) {
				if (raid10_add_disk(mddev, rdev) == 0) {
					if (rdev->raid_disk >=
					    conf->prev.raid_disks)
						set_bit(In_sync, &rdev->flags);
					else
						rdev->recovery_offset = 0;

					if (sysfs_link_rdev(mddev, rdev))
						/* Failure here  is OK */;
				}
			} else if (rdev->raid_disk >= conf->prev.raid_disks
				   && !test_bit(Faulty, &rdev->flags)) {
				/* This is a spare that was manually added */
				set_bit(In_sync, &rdev->flags);
			}
	}
	/* When a reshape changes the number of devices,
	 * ->degraded is measured against the larger of the
	 * pre and  post numbers.
	 */
	spin_lock_irq(&conf->device_lock);
	mddev->degraded = calc_degraded(conf);
	spin_unlock_irq(&conf->device_lock);
	mddev->raid_disks = conf->geo.raid_disks;
	mddev->reshape_position = conf->reshape_progress;
	set_bit(MD_CHANGE_DEVS, &mddev->flags);

	clear_bit(MD_RECOVERY_SYNC, &mddev->recovery);
	clear_bit(MD_RECOVERY_CHECK, &mddev->recovery);
	set_bit(MD_RECOVERY_RESHAPE, &mddev->recovery);
	set_bit(MD_RECOVERY_RUNNING, &mddev->recovery);

	mddev->sync_thread = md_register_thread(md_do_sync, mddev,
						"reshape");
	if (!mddev->sync_thread) {
		ret = -EAGAIN;
		goto abort;
	}
	conf->reshape_checkpoint = jiffies;
	md_wakeup_thread(mddev->sync_thread);
	md_new_event(mddev);
	return 0;

abort:
	mddev->recovery = 0;
	spin_lock_irq(&conf->device_lock);
	conf->geo = conf->prev;
	mddev->raid_disks = conf->geo.raid_disks;
	rdev_for_each(rdev, mddev)
		rdev->new_data_offset = rdev->data_offset;
	smp_wmb();
	conf->reshape_progress = MaxSector;
	mddev->reshape_position = MaxSector;
	spin_unlock_irq(&conf->device_lock);
	return ret;
}

/* Calculate the last device-address that could contain
 * any block from the chunk that includes the array-address 's'
 * and report the next address.
 * i.e. the address returned will be chunk-aligned and after
 * any data that is in the chunk containing 's'.
 */
static sector_t last_dev_address(sector_t s, struct geom *geo)
{
	s = (s | geo->chunk_mask) + 1;
	s >>= geo->chunk_shift;
	s *= geo->near_copies;
	s = DIV_ROUND_UP_SECTOR_T(s, geo->raid_disks);
	s *= geo->far_copies;
	s <<= geo->chunk_shift;
	return s;
}

/* Calculate the first device-address that could contain
 * any block from the chunk that includes the array-address 's'.
 * This too will be the start of a chunk
 */
static sector_t first_dev_address(sector_t s, struct geom *geo)
{
	s >>= geo->chunk_shift;
	s *= geo->near_copies;
	sector_div(s, geo->raid_disks);
	s *= geo->far_copies;
	s <<= geo->chunk_shift;
	return s;
}

static sector_t reshape_request(struct mddev *mddev, sector_t sector_nr,
				int *skipped)
{
	/* We simply copy at most one chunk (smallest of old and new)
	 * at a time, possibly less if that exceeds RESYNC_PAGES,
	 * or we hit a bad block or something.
	 * This might mean we pause for normal IO in the middle of
	 * a chunk, but that is not a problem was mddev->reshape_position
	 * can record any location.
	 *
	 * If we will want to write to a location that isn't
	 * yet recorded as 'safe' (i.e. in metadata on disk) then
	 * we need to flush all reshape requests and update the metadata.
	 *
	 * When reshaping forwards (e.g. to more devices), we interpret
	 * 'safe' as the earliest block which might not have been copied
	 * down yet.  We divide this by previous stripe size and multiply
	 * by previous stripe length to get lowest device offset that we
	 * cannot write to yet.
	 * We interpret 'sector_nr' as an address that we want to write to.
	 * From this we use last_device_address() to find where we might
	 * write to, and first_device_address on the  'safe' position.
	 * If this 'next' write position is after the 'safe' position,
	 * we must update the metadata to increase the 'safe' position.
	 *
	 * When reshaping backwards, we round in the opposite direction
	 * and perform the reverse test:  next write position must not be
	 * less than current safe position.
	 *
	 * In all this the minimum difference in data offsets
	 * (conf->offset_diff - always positive) allows a bit of slack,
	 * so next can be after 'safe', but not by more than offset_disk
	 *
	 * We need to prepare all the bios here before we start any IO
	 * to ensure the size we choose is acceptable to all devices.
	 * The means one for each copy for write-out and an extra one for
	 * read-in.
	 * We store the read-in bio in ->master_bio and the others in
	 * ->devs[x].bio and ->devs[x].repl_bio.
	 */
	struct r10conf *conf = mddev->private;
	struct r10bio *r10_bio;
	sector_t next, safe, last;
	int max_sectors;
	int nr_sectors;
	int s;
	struct md_rdev *rdev;
	int need_flush = 0;
	struct bio *blist;
	struct bio *bio, *read_bio;
	int sectors_done = 0;

	if (sector_nr == 0) {
		/* If restarting in the middle, skip the initial sectors */
		if (mddev->reshape_backwards &&
		    conf->reshape_progress < raid10_size(mddev, 0, 0)) {
			sector_nr = (raid10_size(mddev, 0, 0)
				     - conf->reshape_progress);
		} else if (!mddev->reshape_backwards &&
			   conf->reshape_progress > 0)
			sector_nr = conf->reshape_progress;
		if (sector_nr) {
			mddev->curr_resync_completed = sector_nr;
			sysfs_notify(&mddev->kobj, NULL, "sync_completed");
			*skipped = 1;
			return sector_nr;
		}
	}

	/* We don't use sector_nr to track where we are up to
	 * as that doesn't work well for ->reshape_backwards.
	 * So just use ->reshape_progress.
	 */
	if (mddev->reshape_backwards) {
		/* 'next' is the earliest device address that we might
		 * write to for this chunk in the new layout
		 */
		next = first_dev_address(conf->reshape_progress - 1,
					 &conf->geo);

		/* 'safe' is the last device address that we might read from
		 * in the old layout after a restart
		 */
		safe = last_dev_address(conf->reshape_safe - 1,
					&conf->prev);

		if (next + conf->offset_diff < safe)
			need_flush = 1;

		last = conf->reshape_progress - 1;
		sector_nr = last & ~(sector_t)(conf->geo.chunk_mask
					       & conf->prev.chunk_mask);
		if (sector_nr + RESYNC_BLOCK_SIZE/512 < last)
			sector_nr = last + 1 - RESYNC_BLOCK_SIZE/512;
	} else {
		/* 'next' is after the last device address that we
		 * might write to for this chunk in the new layout
		 */
		next = last_dev_address(conf->reshape_progress, &conf->geo);

		/* 'safe' is the earliest device address that we might
		 * read from in the old layout after a restart
		 */
		safe = first_dev_address(conf->reshape_safe, &conf->prev);

		/* Need to update metadata if 'next' might be beyond 'safe'
		 * as that would possibly corrupt data
		 */
		if (next > safe + conf->offset_diff)
			need_flush = 1;

		sector_nr = conf->reshape_progress;
		last  = sector_nr | (conf->geo.chunk_mask
				     & conf->prev.chunk_mask);

		if (sector_nr + RESYNC_BLOCK_SIZE/512 <= last)
			last = sector_nr + RESYNC_BLOCK_SIZE/512 - 1;
	}

	if (need_flush ||
	    time_after(jiffies, conf->reshape_checkpoint + 10*HZ)) {
		/* Need to update reshape_position in metadata */
		wait_barrier(conf);
		mddev->reshape_position = conf->reshape_progress;
		if (mddev->reshape_backwards)
			mddev->curr_resync_completed = raid10_size(mddev, 0, 0)
				- conf->reshape_progress;
		else
			mddev->curr_resync_completed = conf->reshape_progress;
		conf->reshape_checkpoint = jiffies;
		set_bit(MD_CHANGE_DEVS, &mddev->flags);
		md_wakeup_thread(mddev->thread);
		wait_event(mddev->sb_wait, mddev->flags == 0 ||
			   test_bit(MD_RECOVERY_INTR, &mddev->recovery));
		if (test_bit(MD_RECOVERY_INTR, &mddev->recovery)) {
			allow_barrier(conf);
			return sectors_done;
		}
		conf->reshape_safe = mddev->reshape_position;
		allow_barrier(conf);
	}

read_more:
	/* Now schedule reads for blocks from sector_nr to last */
	r10_bio = mempool_alloc(conf->r10buf_pool, GFP_NOIO);
	r10_bio->state = 0;
	raise_barrier(conf, sectors_done != 0);
	atomic_set(&r10_bio->remaining, 0);
	r10_bio->mddev = mddev;
	r10_bio->sector = sector_nr;
	set_bit(R10BIO_IsReshape, &r10_bio->state);
	r10_bio->sectors = last - sector_nr + 1;
	rdev = read_balance(conf, r10_bio, &max_sectors);
	BUG_ON(!test_bit(R10BIO_Previous, &r10_bio->state));

	if (!rdev) {
		/* Cannot read from here, so need to record bad blocks
		 * on all the target devices.
		 */
		// FIXME
		mempool_free(r10_bio, conf->r10buf_pool);
		set_bit(MD_RECOVERY_INTR, &mddev->recovery);
		return sectors_done;
	}

	read_bio = bio_alloc_mddev(GFP_KERNEL, RESYNC_PAGES, mddev);

	read_bio->bi_bdev = rdev->bdev;
	read_bio->bi_iter.bi_sector = (r10_bio->devs[r10_bio->read_slot].addr
			       + rdev->data_offset);
	read_bio->bi_private = r10_bio;
	read_bio->bi_end_io = end_sync_read;
	read_bio->bi_rw = READ;
	read_bio->bi_flags &= (~0UL << BIO_RESET_BITS);
	__set_bit(BIO_UPTODATE, &read_bio->bi_flags);
	read_bio->bi_vcnt = 0;
	read_bio->bi_iter.bi_size = 0;
	r10_bio->master_bio = read_bio;
	r10_bio->read_slot = r10_bio->devs[r10_bio->read_slot].devnum;

	/* Now find the locations in the new layout */
	__raid10_find_phys(&conf->geo, r10_bio);

	blist = read_bio;
	read_bio->bi_next = NULL;

	for (s = 0; s < conf->copies*2; s++) {
		struct bio *b;
		int d = r10_bio->devs[s/2].devnum;
		struct md_rdev *rdev2;
		if (s&1) {
			rdev2 = conf->mirrors[d].replacement;
			b = r10_bio->devs[s/2].repl_bio;
		} else {
			rdev2 = conf->mirrors[d].rdev;
			b = r10_bio->devs[s/2].bio;
		}
		if (!rdev2 || test_bit(Faulty, &rdev2->flags))
			continue;

		bio_reset(b);
		b->bi_bdev = rdev2->bdev;
		b->bi_iter.bi_sector = r10_bio->devs[s/2].addr +
			rdev2->new_data_offset;
		b->bi_private = r10_bio;
		b->bi_end_io = end_reshape_write;
		b->bi_rw = WRITE;
		b->bi_next = blist;
		blist = b;
	}

	/* Now add as many pages as possible to all of these bios. */

	nr_sectors = 0;
	for (s = 0 ; s < max_sectors; s += PAGE_SIZE >> 9) {
		struct page *page = r10_bio->devs[0].bio->bi_io_vec[s/(PAGE_SIZE>>9)].bv_page;
		int len = (max_sectors - s) << 9;
		if (len > PAGE_SIZE)
			len = PAGE_SIZE;
		for (bio = blist; bio ; bio = bio->bi_next) {
			struct bio *bio2;
			if (bio_add_page(bio, page, len, 0))
				continue;

			/* Didn't fit, must stop */
			for (bio2 = blist;
			     bio2 && bio2 != bio;
			     bio2 = bio2->bi_next) {
				/* Remove last page from this bio */
				bio2->bi_vcnt--;
				bio2->bi_iter.bi_size -= len;
				__clear_bit(BIO_SEG_VALID, &bio2->bi_flags);
			}
			goto bio_full;
		}
		sector_nr += len >> 9;
		nr_sectors += len >> 9;
	}
bio_full:
	r10_bio->sectors = nr_sectors;

	/* Now submit the read */
	md_sync_acct(read_bio->bi_bdev, r10_bio->sectors);
	atomic_inc(&r10_bio->remaining);
	read_bio->bi_next = NULL;
	generic_make_request(read_bio);
	sector_nr += nr_sectors;
	sectors_done += nr_sectors;
	if (sector_nr <= last)
		goto read_more;

	/* Now that we have done the whole section we can
	 * update reshape_progress
	 */
	if (mddev->reshape_backwards)
		conf->reshape_progress -= sectors_done;
	else
		conf->reshape_progress += sectors_done;

	return sectors_done;
}

static void end_reshape_request(struct r10bio *r10_bio);
static int handle_reshape_read_error(struct mddev *mddev,
				     struct r10bio *r10_bio);
static void reshape_request_write(struct mddev *mddev, struct r10bio *r10_bio)
{
	/* Reshape read completed.  Hopefully we have a block
	 * to write out.
	 * If we got a read error then we do sync 1-page reads from
	 * elsewhere until we find the data - or give up.
	 */
	struct r10conf *conf = mddev->private;
	int s;

	if (!test_bit(R10BIO_Uptodate, &r10_bio->state))
		if (handle_reshape_read_error(mddev, r10_bio) < 0) {
			/* Reshape has been aborted */
			md_done_sync(mddev, r10_bio->sectors, 0);
			return;
		}

	/* We definitely have the data in the pages, schedule the
	 * writes.
	 */
	atomic_set(&r10_bio->remaining, 1);
	for (s = 0; s < conf->copies*2; s++) {
		struct bio *b;
		int d = r10_bio->devs[s/2].devnum;
		struct md_rdev *rdev;
		if (s&1) {
			rdev = conf->mirrors[d].replacement;
			b = r10_bio->devs[s/2].repl_bio;
		} else {
			rdev = conf->mirrors[d].rdev;
			b = r10_bio->devs[s/2].bio;
		}
		if (!rdev || test_bit(Faulty, &rdev->flags))
			continue;
		atomic_inc(&rdev->nr_pending);
		md_sync_acct(b->bi_bdev, r10_bio->sectors);
		atomic_inc(&r10_bio->remaining);
		b->bi_next = NULL;
		generic_make_request(b);
	}
	end_reshape_request(r10_bio);
}

static void end_reshape(struct r10conf *conf)
{
	if (test_bit(MD_RECOVERY_INTR, &conf->mddev->recovery))
		return;

	spin_lock_irq(&conf->device_lock);
	conf->prev = conf->geo;
	md_finish_reshape(conf->mddev);
	smp_wmb();
	conf->reshape_progress = MaxSector;
	spin_unlock_irq(&conf->device_lock);

	/* read-ahead size must cover two whole stripes, which is
	 * 2 * (datadisks) * chunksize where 'n' is the number of raid devices
	 */
	if (conf->mddev->queue) {
		int stripe = conf->geo.raid_disks *
			((conf->mddev->chunk_sectors << 9) / PAGE_SIZE);
		stripe /= conf->geo.near_copies;
		if (conf->mddev->queue->backing_dev_info.ra_pages < 2 * stripe)
			conf->mddev->queue->backing_dev_info.ra_pages = 2 * stripe;
	}
	conf->fullsync = 0;
}

static int handle_reshape_read_error(struct mddev *mddev,
				     struct r10bio *r10_bio)
{
	/* Use sync reads to get the blocks from somewhere else */
	int sectors = r10_bio->sectors;
	struct r10conf *conf = mddev->private;
	struct {
		struct r10bio r10_bio;
		struct r10dev devs[conf->copies];
	} on_stack;
	struct r10bio *r10b = &on_stack.r10_bio;
	int slot = 0;
	int idx = 0;
	struct bio_vec *bvec = r10_bio->master_bio->bi_io_vec;

	r10b->sector = r10_bio->sector;
	__raid10_find_phys(&conf->prev, r10b);

	while (sectors) {
		int s = sectors;
		int success = 0;
		int first_slot = slot;

		if (s > (PAGE_SIZE >> 9))
			s = PAGE_SIZE >> 9;

		while (!success) {
			int d = r10b->devs[slot].devnum;
			struct md_rdev *rdev = conf->mirrors[d].rdev;
			sector_t addr;
			if (rdev == NULL ||
			    test_bit(Faulty, &rdev->flags) ||
			    !test_bit(In_sync, &rdev->flags))
				goto failed;

			addr = r10b->devs[slot].addr + idx * PAGE_SIZE;
			success = sync_page_io(rdev,
					       addr,
					       s << 9,
					       bvec[idx].bv_page,
					       READ, false);
			if (success)
				break;
		failed:
			slot++;
			if (slot >= conf->copies)
				slot = 0;
			if (slot == first_slot)
				break;
		}
		if (!success) {
			/* couldn't read this block, must give up */
			set_bit(MD_RECOVERY_INTR,
				&mddev->recovery);
			return -EIO;
		}
		sectors -= s;
		idx++;
	}
	return 0;
}

static void end_reshape_write(struct bio *bio, int error)
{
	int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct r10bio *r10_bio = bio->bi_private;
	struct mddev *mddev = r10_bio->mddev;
	struct r10conf *conf = mddev->private;
	int d;
	int slot;
	int repl;
	struct md_rdev *rdev = NULL;

	d = find_bio_disk(conf, r10_bio, bio, &slot, &repl);
	if (repl)
		rdev = conf->mirrors[d].replacement;
	if (!rdev) {
		smp_mb();
		rdev = conf->mirrors[d].rdev;
	}

	if (!uptodate) {
		/* FIXME should record badblock */
		md_error(mddev, rdev);
	}

	rdev_dec_pending(rdev, mddev);
	end_reshape_request(r10_bio);
}

static void end_reshape_request(struct r10bio *r10_bio)
{
	if (!atomic_dec_and_test(&r10_bio->remaining))
		return;
	md_done_sync(r10_bio->mddev, r10_bio->sectors, 1);
	bio_put(r10_bio->master_bio);
	put_buf(r10_bio);
}

static void raid10_finish_reshape(struct mddev *mddev)
{
	struct r10conf *conf = mddev->private;

	if (test_bit(MD_RECOVERY_INTR, &mddev->recovery))
		return;

	if (mddev->delta_disks > 0) {
		sector_t size = raid10_size(mddev, 0, 0);
		md_set_array_sectors(mddev, size);
		if (mddev->recovery_cp > mddev->resync_max_sectors) {
			mddev->recovery_cp = mddev->resync_max_sectors;
			set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
		}
		mddev->resync_max_sectors = size;
		set_capacity(mddev->gendisk, mddev->array_sectors);
		revalidate_disk(mddev->gendisk);
	} else {
		int d;
		for (d = conf->geo.raid_disks ;
		     d < conf->geo.raid_disks - mddev->delta_disks;
		     d++) {
			struct md_rdev *rdev = conf->mirrors[d].rdev;
			if (rdev)
				clear_bit(In_sync, &rdev->flags);
			rdev = conf->mirrors[d].replacement;
			if (rdev)
				clear_bit(In_sync, &rdev->flags);
		}
	}
	mddev->layout = mddev->new_layout;
	mddev->chunk_sectors = 1 << conf->geo.chunk_shift;
	mddev->reshape_position = MaxSector;
	mddev->delta_disks = 0;
	mddev->reshape_backwards = 0;
}

static struct md_personality raid10_personality =
{
	.name		= "raid10",
	.level		= 10,
	.owner		= THIS_MODULE,
	.make_request	= make_request,
	.run		= run,
	.stop		= stop,
	.status		= status,
	.error_handler	= error,
	.hot_add_disk	= raid10_add_disk,
	.hot_remove_disk= raid10_remove_disk,
	.spare_active	= raid10_spare_active,
	.sync_request	= sync_request,
	.quiesce	= raid10_quiesce,
	.size		= raid10_size,
	.resize		= raid10_resize,
	.takeover	= raid10_takeover,
	.check_reshape	= raid10_check_reshape,
	.start_reshape	= raid10_start_reshape,
	.finish_reshape	= raid10_finish_reshape,
};

static int __init raid_init(void)
{
	return register_md_personality(&raid10_personality);
}

static void raid_exit(void)
{
	unregister_md_personality(&raid10_personality);
}

module_init(raid_init);
module_exit(raid_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RAID10 (striped mirror) personality for MD");
MODULE_ALIAS("md-personality-9"); /* RAID10 */
MODULE_ALIAS("md-raid10");
MODULE_ALIAS("md-level-10");

module_param(max_queued_requests, int, S_IRUGO|S_IWUSR);
