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
	block >>= conf->preshift;
	(void)sector_div(block, conf->hash_spacing);
	hash = conf->hash_table[block];

	while ((sector>>1) >= (hash->size + hash->offset))
		hash++;
	return hash;
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
	mddev_t *mddev = q->queuedata;
	dev_info_t *dev0;
	unsigned long maxsectors, bio_sectors = bvm->bi_size >> 9;
	sector_t sector = bvm->bi_sector + get_start_sect(bvm->bi_bdev);

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

static void linear_unplug(struct request_queue *q)
{
	mddev_t *mddev = q->queuedata;
	linear_conf_t *conf = mddev_to_conf(mddev);
	int i;

	for (i=0; i < mddev->raid_disks; i++) {
		struct request_queue *r_queue = bdev_get_queue(conf->disks[i].rdev->bdev);
		blk_unplug(r_queue);
	}
}

static int linear_congested(void *data, int bits)
{
	mddev_t *mddev = data;
	linear_conf_t *conf = mddev_to_conf(mddev);
	int i, ret = 0;

	for (i = 0; i < mddev->raid_disks && !ret ; i++) {
		struct request_queue *q = bdev_get_queue(conf->disks[i].rdev->bdev);
		ret |= bdi_congested(&q->backing_dev_info, bits);
	}
	return ret;
}

static linear_conf_t *linear_conf(mddev_t *mddev, int raid_disks)
{
	linear_conf_t *conf;
	dev_info_t **table;
	mdk_rdev_t *rdev;
	int i, nb_zone, cnt;
	sector_t min_spacing;
	sector_t curr_offset;
	struct list_head *tmp;

	conf = kzalloc (sizeof (*conf) + raid_disks*sizeof(dev_info_t),
			GFP_KERNEL);
	if (!conf)
		return NULL;

	cnt = 0;
	conf->array_sectors = 0;

	rdev_for_each(rdev, tmp, mddev) {
		int j = rdev->raid_disk;
		dev_info_t *disk = conf->disks + j;

		if (j < 0 || j >= raid_disks || disk->rdev) {
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
		conf->array_sectors += rdev->size * 2;

		cnt++;
	}
	if (cnt != raid_disks) {
		printk("linear: not enough drives present. Aborting!\n");
		goto out;
	}

	min_spacing = conf->array_sectors / 2;
	sector_div(min_spacing, PAGE_SIZE/sizeof(struct dev_info *));

	/* min_spacing is the minimum spacing that will fit the hash
	 * table in one PAGE.  This may be much smaller than needed.
	 * We find the smallest non-terminal set of consecutive devices
	 * that is larger than min_spacing as use the size of that as
	 * the actual spacing
	 */
	conf->hash_spacing = conf->array_sectors / 2;
	for (i=0; i < cnt-1 ; i++) {
		sector_t sz = 0;
		int j;
		for (j = i; j < cnt - 1 && sz < min_spacing; j++)
			sz += conf->disks[j].size;
		if (sz >= min_spacing && sz < conf->hash_spacing)
			conf->hash_spacing = sz;
	}

	/* hash_spacing may be too large for sector_div to work with,
	 * so we might need to pre-shift
	 */
	conf->preshift = 0;
	if (sizeof(sector_t) > sizeof(u32)) {
		sector_t space = conf->hash_spacing;
		while (space > (sector_t)(~(u32)0)) {
			space >>= 1;
			conf->preshift++;
		}
	}
	/*
	 * This code was restructured to work around a gcc-2.95.3 internal
	 * compiler error.  Alter it with care.
	 */
	{
		sector_t sz;
		unsigned round;
		unsigned long base;

		sz = conf->array_sectors >> (conf->preshift + 1);
		sz += 1; /* force round-up */
		base = conf->hash_spacing >> conf->preshift;
		round = sector_div(sz, base);
		nb_zone = sz + (round ? 1 : 0);
	}
	BUG_ON(nb_zone > PAGE_SIZE / sizeof(struct dev_info *));

	conf->hash_table = kmalloc (sizeof (struct dev_info *) * nb_zone,
					GFP_KERNEL);
	if (!conf->hash_table)
		goto out;

	/*
	 * Here we generate the linear hash table
	 * First calculate the device offsets.
	 */
	conf->disks[0].offset = 0;
	for (i = 1; i < raid_disks; i++)
		conf->disks[i].offset =
			conf->disks[i-1].offset +
			conf->disks[i-1].size;

	table = conf->hash_table;
	curr_offset = 0;
	i = 0;
	for (curr_offset = 0;
	     curr_offset < conf->array_sectors / 2;
	     curr_offset += conf->hash_spacing) {

		while (i < raid_disks-1 &&
		       curr_offset >= conf->disks[i+1].offset)
			i++;

		*table ++ = conf->disks + i;
	}

	if (conf->preshift) {
		conf->hash_spacing >>= conf->preshift;
		/* round hash_spacing up so that when we divide by it,
		 * we err on the side of "too-low", which is safest.
		 */
		conf->hash_spacing++;
	}

	BUG_ON(table - conf->hash_table > nb_zone);

	return conf;

out:
	kfree(conf);
	return NULL;
}

static int linear_run (mddev_t *mddev)
{
	linear_conf_t *conf;

	mddev->queue->queue_lock = &mddev->queue->__queue_lock;
	conf = linear_conf(mddev, mddev->raid_disks);

	if (!conf)
		return 1;
	mddev->private = conf;
	mddev->array_sectors = conf->array_sectors;

	blk_queue_merge_bvec(mddev->queue, linear_mergeable_bvec);
	mddev->queue->unplug_fn = linear_unplug;
	mddev->queue->backing_dev_info.congested_fn = linear_congested;
	mddev->queue->backing_dev_info.congested_data = mddev;
	return 0;
}

static int linear_add(mddev_t *mddev, mdk_rdev_t *rdev)
{
	/* Adding a drive to a linear array allows the array to grow.
	 * It is permitted if the new drive has a matching superblock
	 * already on it, with raid_disk equal to raid_disks.
	 * It is achieved by creating a new linear_private_data structure
	 * and swapping it in in-place of the current one.
	 * The current one is never freed until the array is stopped.
	 * This avoids races.
	 */
	linear_conf_t *newconf;

	if (rdev->saved_raid_disk != mddev->raid_disks)
		return -EINVAL;

	rdev->raid_disk = rdev->saved_raid_disk;

	newconf = linear_conf(mddev,mddev->raid_disks+1);

	if (!newconf)
		return -ENOMEM;

	newconf->prev = mddev_to_conf(mddev);
	mddev->private = newconf;
	mddev->raid_disks++;
	mddev->array_sectors = newconf->array_sectors;
	set_capacity(mddev->gendisk, mddev->array_sectors);
	return 0;
}

static int linear_stop (mddev_t *mddev)
{
	linear_conf_t *conf = mddev_to_conf(mddev);
  
	blk_sync_queue(mddev->queue); /* the unplug fn references 'conf'*/
	do {
		linear_conf_t *t = conf->prev;
		kfree(conf->hash_table);
		kfree(conf);
		conf = t;
	} while (conf);

	return 0;
}

static int linear_make_request (struct request_queue *q, struct bio *bio)
{
	const int rw = bio_data_dir(bio);
	mddev_t *mddev = q->queuedata;
	dev_info_t *tmp_dev;
	sector_t block;

	if (unlikely(bio_barrier(bio))) {
		bio_endio(bio, -EOPNOTSUPP);
		return 0;
	}

	disk_stat_inc(mddev->gendisk, ios[rw]);
	disk_stat_add(mddev->gendisk, sectors[rw], bio_sectors(bio));

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
		bio_io_error(bio);
		return 0;
	}
	if (unlikely(bio->bi_sector + (bio->bi_size >> 9) >
		     (tmp_dev->offset + tmp_dev->size)<<1)) {
		/* This bio crosses a device boundary, so we have to
		 * split it.
		 */
		struct bio_pair *bp;
		bp = bio_split(bio, bio_split_pool,
			       ((tmp_dev->offset + tmp_dev->size)<<1) - bio->bi_sector);
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
	for (j = 0; j < mddev->raid_disks; j++)
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


static struct mdk_personality linear_personality =
{
	.name		= "linear",
	.level		= LEVEL_LINEAR,
	.owner		= THIS_MODULE,
	.make_request	= linear_make_request,
	.run		= linear_run,
	.stop		= linear_stop,
	.status		= linear_status,
	.hot_add_disk	= linear_add,
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
MODULE_ALIAS("md-personality-1"); /* LINEAR - deprecated*/
MODULE_ALIAS("md-linear");
MODULE_ALIAS("md-level--1");
