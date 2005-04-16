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

#include <linux/module.h>

#include <linux/raid/md.h>
#include <linux/slab.h>
#include <linux/raid/linear.h>

#define MAJOR_NR MD_MAJOR
#define MD_DRIVER
#define MD_PERSONALITY

/*
 * find which device holds a particular offset 
 */
static inline dev_info_t *which_dev(mddev_t *mddev, sector_t sector)
{
	dev_info_t *hash;
	linear_conf_t *conf = mddev_to_conf(mddev);
	sector_t block = sector >> 1;

	/*
	 * sector_div(a,b) returns the remainer and sets a to a/b
	 */
	(void)sector_div(block, conf->smallest->size);
	hash = conf->hash_table[block];

	while ((sector>>1) >= (hash->size + hash->offset))
		hash++;
	return hash;
}

/**
 *	linear_mergeable_bvec -- tell bio layer if a two requests can be merged
 *	@q: request queue
 *	@bio: the buffer head that's been built up so far
 *	@biovec: the request that could be merged to it.
 *
 *	Return amount of bytes we can take at this offset
 */
static int linear_mergeable_bvec(request_queue_t *q, struct bio *bio, struct bio_vec *biovec)
{
	mddev_t *mddev = q->queuedata;
	dev_info_t *dev0;
	unsigned long maxsectors, bio_sectors = bio->bi_size >> 9;
	sector_t sector = bio->bi_sector + get_start_sect(bio->bi_bdev);

	dev0 = which_dev(mddev, sector);
	maxsectors = (dev0->size << 1) - (sector - (dev0->offset<<1));

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

static void linear_unplug(request_queue_t *q)
{
	mddev_t *mddev = q->queuedata;
	linear_conf_t *conf = mddev_to_conf(mddev);
	int i;

	for (i=0; i < mddev->raid_disks; i++) {
		request_queue_t *r_queue = bdev_get_queue(conf->disks[i].rdev->bdev);
		if (r_queue->unplug_fn)
			r_queue->unplug_fn(r_queue);
	}
}

static int linear_issue_flush(request_queue_t *q, struct gendisk *disk,
			      sector_t *error_sector)
{
	mddev_t *mddev = q->queuedata;
	linear_conf_t *conf = mddev_to_conf(mddev);
	int i, ret = 0;

	for (i=0; i < mddev->raid_disks && ret == 0; i++) {
		struct block_device *bdev = conf->disks[i].rdev->bdev;
		request_queue_t *r_queue = bdev_get_queue(bdev);

		if (!r_queue->issue_flush_fn)
			ret = -EOPNOTSUPP;
		else
			ret = r_queue->issue_flush_fn(r_queue, bdev->bd_disk, error_sector);
	}
	return ret;
}

static int linear_run (mddev_t *mddev)
{
	linear_conf_t *conf;
	dev_info_t **table;
	mdk_rdev_t *rdev;
	int i, nb_zone, cnt;
	sector_t start;
	sector_t curr_offset;
	struct list_head *tmp;

	conf = kmalloc (sizeof (*conf) + mddev->raid_disks*sizeof(dev_info_t),
			GFP_KERNEL);
	if (!conf)
		goto out;
	memset(conf, 0, sizeof(*conf) + mddev->raid_disks*sizeof(dev_info_t));
	mddev->private = conf;

	/*
	 * Find the smallest device.
	 */

	conf->smallest = NULL;
	cnt = 0;
	mddev->array_size = 0;

	ITERATE_RDEV(mddev,rdev,tmp) {
		int j = rdev->raid_disk;
		dev_info_t *disk = conf->disks + j;

		if (j < 0 || j > mddev->raid_disks || disk->rdev) {
			printk("linear: disk numbering problem. Aborting!\n");
			goto out;
		}

		disk->rdev = rdev;

		blk_queue_stack_limits(mddev->queue,
				       rdev->bdev->bd_disk->queue);
		/* as we don't honour merge_bvec_fn, we must never risk
		 * violating it, so limit ->max_sector to one PAGE, as
		 * a one page request is never in violation.
		 */
		if (rdev->bdev->bd_disk->queue->merge_bvec_fn &&
		    mddev->queue->max_sectors > (PAGE_SIZE>>9))
			blk_queue_max_sectors(mddev->queue, PAGE_SIZE>>9);

		disk->size = rdev->size;
		mddev->array_size += rdev->size;

		if (!conf->smallest || (disk->size < conf->smallest->size))
			conf->smallest = disk;
		cnt++;
	}
	if (cnt != mddev->raid_disks) {
		printk("linear: not enough drives present. Aborting!\n");
		goto out;
	}

	/*
	 * This code was restructured to work around a gcc-2.95.3 internal
	 * compiler error.  Alter it with care.
	 */
	{
		sector_t sz;
		unsigned round;
		unsigned long base;

		sz = mddev->array_size;
		base = conf->smallest->size;
		round = sector_div(sz, base);
		nb_zone = conf->nr_zones = sz + (round ? 1 : 0);
	}
			
	conf->hash_table = kmalloc (sizeof (dev_info_t*) * nb_zone,
					GFP_KERNEL);
	if (!conf->hash_table)
		goto out;

	/*
	 * Here we generate the linear hash table
	 */
	table = conf->hash_table;
	start = 0;
	curr_offset = 0;
	for (i = 0; i < cnt; i++) {
		dev_info_t *disk = conf->disks + i;

		disk->offset = curr_offset;
		curr_offset += disk->size;

		/* 'curr_offset' is the end of this disk
		 * 'start' is the start of table
		 */
		while (start < curr_offset) {
			*table++ = disk;
			start += conf->smallest->size;
		}
	}
	if (table-conf->hash_table != nb_zone)
		BUG();

	blk_queue_merge_bvec(mddev->queue, linear_mergeable_bvec);
	mddev->queue->unplug_fn = linear_unplug;
	mddev->queue->issue_flush_fn = linear_issue_flush;
	return 0;

out:
	if (conf)
		kfree(conf);
	return 1;
}

static int linear_stop (mddev_t *mddev)
{
	linear_conf_t *conf = mddev_to_conf(mddev);
  
	blk_sync_queue(mddev->queue); /* the unplug fn references 'conf'*/
	kfree(conf->hash_table);
	kfree(conf);

	return 0;
}

static int linear_make_request (request_queue_t *q, struct bio *bio)
{
	mddev_t *mddev = q->queuedata;
	dev_info_t *tmp_dev;
	sector_t block;

	if (bio_data_dir(bio)==WRITE) {
		disk_stat_inc(mddev->gendisk, writes);
		disk_stat_add(mddev->gendisk, write_sectors, bio_sectors(bio));
	} else {
		disk_stat_inc(mddev->gendisk, reads);
		disk_stat_add(mddev->gendisk, read_sectors, bio_sectors(bio));
	}

	tmp_dev = which_dev(mddev, bio->bi_sector);
	block = bio->bi_sector >> 1;
    
	if (unlikely(block >= (tmp_dev->size + tmp_dev->offset)
		     || block < tmp_dev->offset)) {
		char b[BDEVNAME_SIZE];

		printk("linear_make_request: Block %llu out of bounds on "
			"dev %s size %llu offset %llu\n",
			(unsigned long long)block,
			bdevname(tmp_dev->rdev->bdev, b),
			(unsigned long long)tmp_dev->size,
		        (unsigned long long)tmp_dev->offset);
		bio_io_error(bio, bio->bi_size);
		return 0;
	}
	if (unlikely(bio->bi_sector + (bio->bi_size >> 9) >
		     (tmp_dev->offset + tmp_dev->size)<<1)) {
		/* This bio crosses a device boundary, so we have to
		 * split it.
		 */
		struct bio_pair *bp;
		bp = bio_split(bio, bio_split_pool, 
			       (bio->bi_sector + (bio->bi_size >> 9) -
				(tmp_dev->offset + tmp_dev->size))<<1);
		if (linear_make_request(q, &bp->bio1))
			generic_make_request(&bp->bio1);
		if (linear_make_request(q, &bp->bio2))
			generic_make_request(&bp->bio2);
		bio_pair_release(bp);
		return 0;
	}
		    
	bio->bi_bdev = tmp_dev->rdev->bdev;
	bio->bi_sector = bio->bi_sector - (tmp_dev->offset << 1) + tmp_dev->rdev->data_offset;

	return 1;
}

static void linear_status (struct seq_file *seq, mddev_t *mddev)
{

#undef MD_DEBUG
#ifdef MD_DEBUG
	int j;
	linear_conf_t *conf = mddev_to_conf(mddev);
	sector_t s = 0;
  
	seq_printf(seq, "      ");
	for (j = 0; j < conf->nr_zones; j++)
	{
		char b[BDEVNAME_SIZE];
		s += conf->smallest_size;
		seq_printf(seq, "[%s",
			   bdevname(conf->hash_table[j][0].rdev->bdev,b));

		while (s > conf->hash_table[j][0].offset +
		           conf->hash_table[j][0].size)
			seq_printf(seq, "/%s] ",
				   bdevname(conf->hash_table[j][1].rdev->bdev,b));
		else
			seq_printf(seq, "] ");
	}
	seq_printf(seq, "\n");
#endif
	seq_printf(seq, " %dk rounding", mddev->chunk_size/1024);
}


static mdk_personality_t linear_personality=
{
	.name		= "linear",
	.owner		= THIS_MODULE,
	.make_request	= linear_make_request,
	.run		= linear_run,
	.stop		= linear_stop,
	.status		= linear_status,
};

static int __init linear_init (void)
{
	return register_md_personality (LINEAR, &linear_personality);
}

static void linear_exit (void)
{
	unregister_md_personality (LINEAR);
}


module_init(linear_init);
module_exit(linear_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS("md-personality-1"); /* LINEAR */
