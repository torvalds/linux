/*
   linear.c : Multiple Devices driver for Linux
	      Copyright (C) 1994-96 Marc ZYNGIER
	      <zyngier@ufr-info-p7.ibp.fr> or
	      <maz@gloups.fdn.fr>

   Linear mode management functions.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

#include <linux/blkdev.h>
#include <linux/raid/md_u.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "md.h"
#include "linear.h"

/*
 * find which device holds a particular offset 
 */
static inline struct dev_info *which_dev(struct mddev *mddev, sector_t sector)
{
	int lo, mid, hi;
	struct linear_conf *conf;

	lo = 0;
	hi = mddev->raid_disks - 1;
	conf = rcu_dereference(mddev->private);

	/*
	 * Binary Search
	 */

	while (hi > lo) {

		mid = (hi + lo) / 2;
		if (sector < conf->disks[mid].end_sector)
			hi = mid;
		else
			lo = mid + 1;
	}

	return conf->disks + lo;
}

/**
 *	linear_mergeable_bvec -- tell bio layer if two requests can be merged
 *	@q: request queue
 *	@bvm: properties of new bio
 *	@biovec: the request that could be merged to it.
 *
 *	Return amount of bytes we can take at this offset
 */
static int linear_mergeable_bvec(struct request_queue *q,
				 struct bvec_merge_data *bvm,
				 struct bio_vec *biovec)
{
	struct mddev *mddev = q->queuedata;
	struct dev_info *dev0;
	unsigned long maxsectors, bio_sectors = bvm->bi_size >> 9;
	sector_t sector = bvm->bi_sector + get_start_sect(bvm->bi_bdev);

	rcu_read_lock();
	dev0 = which_dev(mddev, sector);
	maxsectors = dev0->end_sector - sector;
	rcu_read_unlock();

	if (maxsectors < bio_sectors)
		maxsectors = 0;
	else
		maxsectors -= bio_sectors;

	if (maxsectors <= (PAGE_SIZE >> 9 ) && bio_sectors == 0)
		return biovec->bv_len;
	/* The bytes available at this offset could be really big,
	 * so we cap at 2^31 to avoid overflow */
	if (maxsectors > (1 << (31-9)))
		return 1<<31;
	return maxsectors << 9;
}

static int linear_congested(void *data, int bits)
{
	struct mddev *mddev = data;
	struct linear_conf *conf;
	int i, ret = 0;

	if (mddev_congested(mddev, bits))
		return 1;

	rcu_read_lock();
	conf = rcu_dereference(mddev->private);

	for (i = 0; i < mddev->raid_disks && !ret ; i++) {
		struct request_queue *q = bdev_get_queue(conf->disks[i].rdev->bdev);
		ret |= bdi_congested(&q->backing_dev_info, bits);
	}

	rcu_read_unlock();
	return ret;
}

static sector_t linear_size(struct mddev *mddev, sector_t sectors, int raid_disks)
{
	struct linear_conf *conf;
	sector_t array_sectors;

	rcu_read_lock();
	conf = rcu_dereference(mddev->private);
	WARN_ONCE(sectors || raid_disks,
		  "%s does not support generic reshape\n", __func__);
	array_sectors = conf->array_sectors;
	rcu_read_unlock();

	return array_sectors;
}

static struct linear_conf *linear_conf(struct mddev *mddev, int raid_disks)
{
	struct linear_conf *conf;
	struct md_rdev *rdev;
	int i, cnt;

	conf = kzalloc (sizeof (*conf) + raid_disks*sizeof(struct dev_info),
			GFP_KERNEL);
	if (!conf)
		return NULL;

	cnt = 0;
	conf->array_sectors = 0;

	list_for_each_entry(rdev, &mddev->disks, same_set) {
		int j = rdev->raid_disk;
		struct dev_info *disk = conf->disks + j;
		sector_t sectors;

		if (j < 0 || j >= raid_disks || disk->rdev) {
			printk(KERN_ERR "md/linear:%s: disk numbering problem. Aborting!\n",
			       mdname(mddev));
			goto out;
		}

		disk->rdev = rdev;
		if (mddev->chunk_sectors) {
			sectors = rdev->sectors;
			sector_div(sectors, mddev->chunk_sectors);
			rdev->sectors = sectors * mddev->chunk_sectors;
		}

		disk_stack_limits(mddev->gendisk, rdev->bdev,
				  rdev->data_offset << 9);
		/* as we don't honour merge_bvec_fn, we must never risk
		 * violating it, so limit max_segments to 1 lying within
		 * a single page.
		 */
		if (rdev->bdev->bd_disk->queue->merge_bvec_fn) {
			blk_queue_max_segments(mddev->queue, 1);
			blk_queue_segment_boundary(mddev->queue,
						   PAGE_CACHE_SIZE - 1);
		}

		conf->array_sectors += rdev->sectors;
		cnt++;

	}
	if (cnt != raid_disks) {
		printk(KERN_ERR "md/linear:%s: not enough drives present. Aborting!\n",
		       mdname(mddev));
		goto out;
	}

	/*
	 * Here we calculate the device offsets.
	 */
	conf->disks[0].end_sector = conf->disks[0].rdev->sectors;

	for (i = 1; i < raid_disks; i++)
		conf->disks[i].end_sector =
			conf->disks[i-1].end_sector +
			conf->disks[i].rdev->sectors;

	return conf;

out:
	kfree(conf);
	return NULL;
}

static int linear_run (struct mddev *mddev)
{
	struct linear_conf *conf;

	if (md_check_no_bitmap(mddev))
		return -EINVAL;
	conf = linear_conf(mddev, mddev->raid_disks);

	if (!conf)
		return 1;
	mddev->private = conf;
	md_set_array_sectors(mddev, linear_size(mddev, 0, 0));

	blk_queue_merge_bvec(mddev->queue, linear_mergeable_bvec);
	mddev->queue->backing_dev_info.congested_fn = linear_congested;
	mddev->queue->backing_dev_info.congested_data = mddev;
	return md_integrity_register(mddev);
}

static int linear_add(struct mddev *mddev, struct md_rdev *rdev)
{
	/* Adding a drive to a linear array allows the array to grow.
	 * It is permitted if the new drive has a matching superblock
	 * already on it, with raid_disk equal to raid_disks.
	 * It is achieved by creating a new linear_private_data structure
	 * and swapping it in in-place of the current one.
	 * The current one is never freed until the array is stopped.
	 * This avoids races.
	 */
	struct linear_conf *newconf, *oldconf;

	if (rdev->saved_raid_disk != mddev->raid_disks)
		return -EINVAL;

	rdev->raid_disk = rdev->saved_raid_disk;
	rdev->saved_raid_disk = -1;

	newconf = linear_conf(mddev,mddev->raid_disks+1);

	if (!newconf)
		return -ENOMEM;

	oldconf = rcu_dereference(mddev->private);
	mddev->raid_disks++;
	rcu_assign_pointer(mddev->private, newconf);
	md_set_array_sectors(mddev, linear_size(mddev, 0, 0));
	set_capacity(mddev->gendisk, mddev->array_sectors);
	revalidate_disk(mddev->gendisk);
	kfree_rcu(oldconf, rcu);
	return 0;
}

static int linear_stop (struct mddev *mddev)
{
	struct linear_conf *conf = mddev->private;

	/*
	 * We do not require rcu protection here since
	 * we hold reconfig_mutex for both linear_add and
	 * linear_stop, so they cannot race.
	 * We should make sure any old 'conf's are properly
	 * freed though.
	 */
	rcu_barrier();
	blk_sync_queue(mddev->queue); /* the unplug fn references 'conf'*/
	kfree(conf);
	mddev->private = NULL;

	return 0;
}

static void linear_make_request(struct mddev *mddev, struct bio *bio)
{
	struct dev_info *tmp_dev;
	sector_t start_sector;

	if (unlikely(bio->bi_rw & REQ_FLUSH)) {
		md_flush_request(mddev, bio);
		return;
	}

	rcu_read_lock();
	tmp_dev = which_dev(mddev, bio->bi_sector);
	start_sector = tmp_dev->end_sector - tmp_dev->rdev->sectors;


	if (unlikely(bio->bi_sector >= (tmp_dev->end_sector)
		     || (bio->bi_sector < start_sector))) {
		char b[BDEVNAME_SIZE];

		printk(KERN_ERR
		       "md/linear:%s: make_request: Sector %llu out of bounds on "
		       "dev %s: %llu sectors, offset %llu\n",
		       mdname(mddev),
		       (unsigned long long)bio->bi_sector,
		       bdevname(tmp_dev->rdev->bdev, b),
		       (unsigned long long)tmp_dev->rdev->sectors,
		       (unsigned long long)start_sector);
		rcu_read_unlock();
		bio_io_error(bio);
		return;
	}
	if (unlikely(bio->bi_sector + (bio->bi_size >> 9) >
		     tmp_dev->end_sector)) {
		/* This bio crosses a device boundary, so we have to
		 * split it.
		 */
		struct bio_pair *bp;
		sector_t end_sector = tmp_dev->end_sector;

		rcu_read_unlock();

		bp = bio_split(bio, end_sector - bio->bi_sector);

		linear_make_request(mddev, &bp->bio1);
		linear_make_request(mddev, &bp->bio2);
		bio_pair_release(bp);
		return;
	}
		    
	bio->bi_bdev = tmp_dev->rdev->bdev;
	bio->bi_sector = bio->bi_sector - start_sector
		+ tmp_dev->rdev->data_offset;
	rcu_read_unlock();
	generic_make_request(bio);
}

static void linear_status (struct seq_file *seq, struct mddev *mddev)
{

	seq_printf(seq, " %dk rounding", mddev->chunk_sectors / 2);
}


static struct md_personality linear_personality =
{
	.name		= "linear",
	.level		= LEVEL_LINEAR,
	.owner		= THIS_MODULE,
	.make_request	= linear_make_request,
	.run		= linear_run,
	.stop		= linear_stop,
	.status		= linear_status,
	.hot_add_disk	= linear_add,
	.size		= linear_size,
};

static int __init linear_init (void)
{
	return register_md_personality (&linear_personality);
}

static void linear_exit (void)
{
	unregister_md_personality (&linear_personality);
}


module_init(linear_init);
module_exit(linear_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Linear device concatenation personality for MD");
MODULE_ALIAS("md-personality-1"); /* LINEAR - deprecated*/
MODULE_ALIAS("md-linear");
MODULE_ALIAS("md-level--1");
