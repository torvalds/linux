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
int dm_linear_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct linear_c *lc;
	unsigned long long tmp;
	char dummy;
	int ret;

	if (argc != 2) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	lc = kmalloc(sizeof(*lc), GFP_KERNEL);
	if (lc == NULL) {
		ti->error = "Cannot allocate linear context";
		return -ENOMEM;
	}

	ret = -EINVAL;
	if (sscanf(argv[1], "%llu%c", &tmp, &dummy) != 1) {
		ti->error = "Invalid device sector";
		goto bad;
	}
	lc->start = tmp;

	ret = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &lc->dev);
	if (ret) {
		ti->error = "Device lookup failed";
		goto bad;
	}

	ti->num_flush_bios = 1;
	ti->num_discard_bios = 1;
	ti->num_write_same_bios = 1;
	ti->private = lc;
	return 0;

      bad:
	kfree(lc);
	return ret;
}
EXPORT_SYMBOL_GPL(dm_linear_ctr);

void dm_linear_dtr(struct dm_target *ti)
{
	struct linear_c *lc = (struct linear_c *) ti->private;

	dm_put_device(ti, lc->dev);
	kfree(lc);
}
EXPORT_SYMBOL_GPL(dm_linear_dtr);

static sector_t linear_map_sector(struct dm_target *ti, sector_t bi_sector)
{
	struct linear_c *lc = ti->private;

	return lc->start + dm_target_offset(ti, bi_sector);
}

static void linear_map_bio(struct dm_target *ti, struct bio *bio)
{
	struct linear_c *lc = ti->private;

	bio->bi_bdev = lc->dev->bdev;
	if (bio_sectors(bio))
		bio->bi_iter.bi_sector =
			linear_map_sector(ti, bio->bi_iter.bi_sector);
}

int dm_linear_map(struct dm_target *ti, struct bio *bio)
{
	linear_map_bio(ti, bio);

	return DM_MAPIO_REMAPPED;
}
EXPORT_SYMBOL_GPL(dm_linear_map);

void dm_linear_status(struct dm_target *ti, status_type_t type,
			  unsigned status_flags, char *result, unsigned maxlen)
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
}
EXPORT_SYMBOL_GPL(dm_linear_status);

int dm_linear_prepare_ioctl(struct dm_target *ti,
		struct block_device **bdev, fmode_t *mode)
{
	struct linear_c *lc = (struct linear_c *) ti->private;
	struct dm_dev *dev = lc->dev;

	*bdev = dev->bdev;

	/*
	 * Only pass ioctls through if the device sizes match exactly.
	 */
	if (lc->start ||
	    ti->len != i_size_read(dev->bdev->bd_inode) >> SECTOR_SHIFT)
		return 1;
	return 0;
}
EXPORT_SYMBOL_GPL(dm_linear_prepare_ioctl);

int dm_linear_iterate_devices(struct dm_target *ti,
				  iterate_devices_callout_fn fn, void *data)
{
	struct linear_c *lc = ti->private;

	return fn(ti, lc->dev, lc->start, ti->len, data);
}
EXPORT_SYMBOL_GPL(dm_linear_iterate_devices);

long dm_linear_direct_access(struct dm_target *ti, sector_t sector,
				 void **kaddr, pfn_t *pfn, long size)
{
	struct linear_c *lc = ti->private;
	struct block_device *bdev = lc->dev->bdev;
	struct blk_dax_ctl dax = {
		.sector = linear_map_sector(ti, sector),
		.size = size,
	};
	long ret;

	ret = bdev_direct_access(bdev, &dax);
	*kaddr = dax.addr;
	*pfn = dax.pfn;

	return ret;
}
EXPORT_SYMBOL_GPL(dm_linear_direct_access);

static struct target_type linear_target = {
	.name   = "linear",
	.version = {1, 3, 0},
	.module = THIS_MODULE,
	.ctr    = dm_linear_ctr,
	.dtr    = dm_linear_dtr,
	.map    = dm_linear_map,
	.status = dm_linear_status,
	.prepare_ioctl = dm_linear_prepare_ioctl,
	.iterate_devices = dm_linear_iterate_devices,
	.direct_access = dm_linear_direct_access,
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
