/*
 * Copyright (C) 2001-2003 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "linear"

/*
 * Linear: maps a linear range of a device.
 */
struct linear_c {
	struct dm_dev *dev;
	sector_t start;
};

/*
 * Construct a linear mapping: <dev_path> <offset>
 */
static int linear_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct linear_c *lc;
	unsigned long long tmp;

	if (argc != 2) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	lc = kmalloc(sizeof(*lc), GFP_KERNEL);
	if (lc == NULL) {
		ti->error = "dm-linear: Cannot allocate linear context";
		return -ENOMEM;
	}

	if (sscanf(argv[1], "%llu", &tmp) != 1) {
		ti->error = "dm-linear: Invalid device sector";
		goto bad;
	}
	lc->start = tmp;

	if (dm_get_device(ti, argv[0], lc->start, ti->len,
			  dm_table_get_mode(ti->table), &lc->dev)) {
		ti->error = "dm-linear: Device lookup failed";
		goto bad;
	}

	ti->num_flush_requests = 1;
	ti->private = lc;
	return 0;

      bad:
	kfree(lc);
	return -EINVAL;
}

static void linear_dtr(struct dm_target *ti)
{
	struct linear_c *lc = (struct linear_c *) ti->private;

	dm_put_device(ti, lc->dev);
	kfree(lc);
}

static sector_t linear_map_sector(struct dm_target *ti, sector_t bi_sector)
{
	struct linear_c *lc = ti->private;

	return lc->start + (bi_sector - ti->begin);
}

static void linear_map_bio(struct dm_target *ti, struct bio *bio)
{
	struct linear_c *lc = ti->private;

	bio->bi_bdev = lc->dev->bdev;
	if (bio_sectors(bio))
		bio->bi_sector = linear_map_sector(ti, bio->bi_sector);
}

static int linear_map(struct dm_target *ti, struct bio *bio,
		      union map_info *map_context)
{
	linear_map_bio(ti, bio);

	return DM_MAPIO_REMAPPED;
}

static int linear_status(struct dm_target *ti, status_type_t type,
			 char *result, unsigned int maxlen)
{
	struct linear_c *lc = (struct linear_c *) ti->private;

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		snprintf(result, maxlen, "%s %llu", lc->dev->name,
				(unsigned long long)lc->start);
		break;
	}
	return 0;
}

static int linear_ioctl(struct dm_target *ti, unsigned int cmd,
			unsigned long arg)
{
	struct linear_c *lc = (struct linear_c *) ti->private;
	return __blkdev_driver_ioctl(lc->dev->bdev, lc->dev->mode, cmd, arg);
}

static int linear_merge(struct dm_target *ti, struct bvec_merge_data *bvm,
			struct bio_vec *biovec, int max_size)
{
	struct linear_c *lc = ti->private;
	struct request_queue *q = bdev_get_queue(lc->dev->bdev);

	if (!q->merge_bvec_fn)
		return max_size;

	bvm->bi_bdev = lc->dev->bdev;
	bvm->bi_sector = linear_map_sector(ti, bvm->bi_sector);

	return min(max_size, q->merge_bvec_fn(q, bvm, biovec));
}

static struct target_type linear_target = {
	.name   = "linear",
	.version= {1, 0, 3},
	.module = THIS_MODULE,
	.ctr    = linear_ctr,
	.dtr    = linear_dtr,
	.map    = linear_map,
	.status = linear_status,
	.ioctl  = linear_ioctl,
	.merge  = linear_merge,
};

int __init dm_linear_init(void)
{
	int r = dm_register_target(&linear_target);

	if (r < 0)
		DMERR("register failed %d", r);

	return r;
}

void dm_linear_exit(void)
{
	dm_unregister_target(&linear_target);
}
