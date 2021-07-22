/*
 * Copyright (C) 2017 Intel Corporation.
 *
 * This file is released under the GPL.
 */

#include "dm.h"

#include <linux/module.h>

struct unstripe_c {
	struct dm_dev *dev;
	sector_t physical_start;

	uint32_t stripes;

	uint32_t unstripe;
	sector_t unstripe_width;
	sector_t unstripe_offset;

	uint32_t chunk_size;
	u8 chunk_shift;
};

#define DM_MSG_PREFIX "unstriped"

static void cleanup_unstripe(struct unstripe_c *uc, struct dm_target *ti)
{
	if (uc->dev)
		dm_put_device(ti, uc->dev);
	kfree(uc);
}

/*
 * Contruct an unstriped mapping.
 * <number of stripes> <chunk size> <stripe #> <dev_path> <offset>
 */
static int unstripe_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct unstripe_c *uc;
	sector_t tmp_len;
	unsigned long long start;
	char dummy;

	if (argc != 5) {
		ti->error = "Invalid number of arguments";
		return -EINVAL;
	}

	uc = kzalloc(sizeof(*uc), GFP_KERNEL);
	if (!uc) {
		ti->error = "Memory allocation for unstriped context failed";
		return -ENOMEM;
	}

	if (kstrtouint(argv[0], 10, &uc->stripes) || !uc->stripes) {
		ti->error = "Invalid stripe count";
		goto err;
	}

	if (kstrtouint(argv[1], 10, &uc->chunk_size) || !uc->chunk_size) {
		ti->error = "Invalid chunk_size";
		goto err;
	}

	if (kstrtouint(argv[2], 10, &uc->unstripe)) {
		ti->error = "Invalid stripe number";
		goto err;
	}

	if (uc->unstripe > uc->stripes && uc->stripes > 1) {
		ti->error = "Please provide stripe between [0, # of stripes]";
		goto err;
	}

	if (dm_get_device(ti, argv[3], dm_table_get_mode(ti->table), &uc->dev)) {
		ti->error = "Couldn't get striped device";
		goto err;
	}

	if (sscanf(argv[4], "%llu%c", &start, &dummy) != 1 || start != (sector_t)start) {
		ti->error = "Invalid striped device offset";
		goto err;
	}
	uc->physical_start = start;

	uc->unstripe_offset = uc->unstripe * uc->chunk_size;
	uc->unstripe_width = (uc->stripes - 1) * uc->chunk_size;
	uc->chunk_shift = is_power_of_2(uc->chunk_size) ? fls(uc->chunk_size) - 1 : 0;

	tmp_len = ti->len;
	if (sector_div(tmp_len, uc->chunk_size)) {
		ti->error = "Target length not divisible by chunk size";
		goto err;
	}

	if (dm_set_target_max_io_len(ti, uc->chunk_size)) {
		ti->error = "Failed to set max io len";
		goto err;
	}

	ti->private = uc;
	return 0;
err:
	cleanup_unstripe(uc, ti);
	return -EINVAL;
}

static void unstripe_dtr(struct dm_target *ti)
{
	struct unstripe_c *uc = ti->private;

	cleanup_unstripe(uc, ti);
}

static sector_t map_to_core(struct dm_target *ti, struct bio *bio)
{
	struct unstripe_c *uc = ti->private;
	sector_t sector = bio->bi_iter.bi_sector;
	sector_t tmp_sector = sector;

	/* Shift us up to the right "row" on the stripe */
	if (uc->chunk_shift)
		tmp_sector >>= uc->chunk_shift;
	else
		sector_div(tmp_sector, uc->chunk_size);

	sector += uc->unstripe_width * tmp_sector;

	/* Account for what stripe we're operating on */
	return sector + uc->unstripe_offset;
}

static int unstripe_map(struct dm_target *ti, struct bio *bio)
{
	struct unstripe_c *uc = ti->private;

	bio_set_dev(bio, uc->dev->bdev);
	bio->bi_iter.bi_sector = map_to_core(ti, bio) + uc->physical_start;

	return DM_MAPIO_REMAPPED;
}

static void unstripe_status(struct dm_target *ti, status_type_t type,
			    unsigned int status_flags, char *result, unsigned int maxlen)
{
	struct unstripe_c *uc = ti->private;
	unsigned int sz = 0;

	switch (type) {
	case STATUSTYPE_INFO:
		break;

	case STATUSTYPE_TABLE:
		DMEMIT("%d %llu %d %s %llu",
		       uc->stripes, (unsigned long long)uc->chunk_size, uc->unstripe,
		       uc->dev->name, (unsigned long long)uc->physical_start);
		break;
	}
}

static int unstripe_iterate_devices(struct dm_target *ti,
				    iterate_devices_callout_fn fn, void *data)
{
	struct unstripe_c *uc = ti->private;

	return fn(ti, uc->dev, uc->physical_start, ti->len, data);
}

static void unstripe_io_hints(struct dm_target *ti,
			       struct queue_limits *limits)
{
	struct unstripe_c *uc = ti->private;

	limits->chunk_sectors = uc->chunk_size;
}

static struct target_type unstripe_target = {
	.name = "unstriped",
	.version = {1, 1, 0},
	.features = DM_TARGET_NOWAIT,
	.module = THIS_MODULE,
	.ctr = unstripe_ctr,
	.dtr = unstripe_dtr,
	.map = unstripe_map,
	.status = unstripe_status,
	.iterate_devices = unstripe_iterate_devices,
	.io_hints = unstripe_io_hints,
};

static int __init dm_unstripe_init(void)
{
	return dm_register_target(&unstripe_target);
}

static void __exit dm_unstripe_exit(void)
{
	dm_unregister_target(&unstripe_target);
}

module_init(dm_unstripe_init);
module_exit(dm_unstripe_exit);

MODULE_DESCRIPTION(DM_NAME " unstriped target");
MODULE_ALIAS("dm-unstriped");
MODULE_AUTHOR("Scott Bauer <scott.bauer@intel.com>");
MODULE_LICENSE("GPL");
