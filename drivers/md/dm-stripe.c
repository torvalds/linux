// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2001-2003 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include <linux/device-mapper.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/dax.h>
#include <linux/slab.h>
#include <linux/log2.h>

static struct workqueue_struct *dm_stripe_wq;

#define DM_MSG_PREFIX "striped"
#define DM_IO_ERROR_THRESHOLD 15

struct stripe {
	struct dm_dev *dev;
	sector_t physical_start;

	atomic_t error_count;
};

struct stripe_c {
	uint32_t stripes;
	int stripes_shift;

	/* The size of this target / num. stripes */
	sector_t stripe_width;

	uint32_t chunk_size;
	int chunk_size_shift;

	/* Needed for handling events */
	struct dm_target *ti;

	/* Work struct used for triggering events*/
	struct work_struct trigger_event;

	struct stripe stripe[] __counted_by(stripes);
};

/*
 * An event is triggered whenever a drive
 * drops out of a stripe volume.
 */
static void trigger_event(struct work_struct *work)
{
	struct stripe_c *sc = container_of(work, struct stripe_c,
					   trigger_event);
	dm_table_event(sc->ti->table);
}

/*
 * Parse a single <dev> <sector> pair
 */
static int get_stripe(struct dm_target *ti, struct stripe_c *sc,
		      unsigned int stripe, char **argv)
{
	unsigned long long start;
	char dummy;
	int ret;

	if (sscanf(argv[1], "%llu%c", &start, &dummy) != 1)
		return -EINVAL;

	ret = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table),
			    &sc->stripe[stripe].dev);
	if (ret)
		return ret;

	sc->stripe[stripe].physical_start = start;

	return 0;
}

/*
 * Construct a striped mapping.
 * <number of stripes> <chunk size> [<dev_path> <offset>]+
 */
static int stripe_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct stripe_c *sc;
	sector_t width, tmp_len;
	uint32_t stripes;
	uint32_t chunk_size;
	int r;
	unsigned int i;

	if (argc < 2) {
		ti->error = "Not enough arguments";
		return -EINVAL;
	}

	if (kstrtouint(argv[0], 10, &stripes) || !stripes) {
		ti->error = "Invalid stripe count";
		return -EINVAL;
	}

	if (kstrtouint(argv[1], 10, &chunk_size) || !chunk_size) {
		ti->error = "Invalid chunk_size";
		return -EINVAL;
	}

	width = ti->len;
	if (sector_div(width, stripes)) {
		ti->error = "Target length not divisible by number of stripes";
		return -EINVAL;
	}

	tmp_len = width;
	if (sector_div(tmp_len, chunk_size)) {
		ti->error = "Target length not divisible by chunk size";
		return -EINVAL;
	}

	/*
	 * Do we have enough arguments for that many stripes ?
	 */
	if (argc != (2 + 2 * stripes)) {
		ti->error = "Not enough destinations specified";
		return -EINVAL;
	}

	sc = kmalloc(struct_size(sc, stripe, stripes), GFP_KERNEL);
	if (!sc) {
		ti->error = "Memory allocation for striped context failed";
		return -ENOMEM;
	}

	INIT_WORK(&sc->trigger_event, trigger_event);

	/* Set pointer to dm target; used in trigger_event */
	sc->ti = ti;
	sc->stripes = stripes;
	sc->stripe_width = width;

	if (stripes & (stripes - 1))
		sc->stripes_shift = -1;
	else
		sc->stripes_shift = __ffs(stripes);

	r = dm_set_target_max_io_len(ti, chunk_size);
	if (r) {
		kfree(sc);
		return r;
	}

	ti->num_flush_bios = stripes;
	ti->num_discard_bios = stripes;
	ti->num_secure_erase_bios = stripes;
	ti->num_write_zeroes_bios = stripes;

	sc->chunk_size = chunk_size;
	if (chunk_size & (chunk_size - 1))
		sc->chunk_size_shift = -1;
	else
		sc->chunk_size_shift = __ffs(chunk_size);

	/*
	 * Get the stripe destinations.
	 */
	for (i = 0; i < stripes; i++) {
		argv += 2;

		r = get_stripe(ti, sc, i, argv);
		if (r < 0) {
			ti->error = "Couldn't parse stripe destination";
			while (i--)
				dm_put_device(ti, sc->stripe[i].dev);
			kfree(sc);
			return r;
		}
		atomic_set(&(sc->stripe[i].error_count), 0);
	}

	ti->private = sc;

	return 0;
}

static void stripe_dtr(struct dm_target *ti)
{
	unsigned int i;
	struct stripe_c *sc = ti->private;

	for (i = 0; i < sc->stripes; i++)
		dm_put_device(ti, sc->stripe[i].dev);

	flush_work(&sc->trigger_event);
	kfree(sc);
}

static void stripe_map_sector(struct stripe_c *sc, sector_t sector,
			      uint32_t *stripe, sector_t *result)
{
	sector_t chunk = dm_target_offset(sc->ti, sector);
	sector_t chunk_offset;

	if (sc->chunk_size_shift < 0)
		chunk_offset = sector_div(chunk, sc->chunk_size);
	else {
		chunk_offset = chunk & (sc->chunk_size - 1);
		chunk >>= sc->chunk_size_shift;
	}

	if (sc->stripes_shift < 0)
		*stripe = sector_div(chunk, sc->stripes);
	else {
		*stripe = chunk & (sc->stripes - 1);
		chunk >>= sc->stripes_shift;
	}

	if (sc->chunk_size_shift < 0)
		chunk *= sc->chunk_size;
	else
		chunk <<= sc->chunk_size_shift;

	*result = chunk + chunk_offset;
}

static void stripe_map_range_sector(struct stripe_c *sc, sector_t sector,
				    uint32_t target_stripe, sector_t *result)
{
	uint32_t stripe;

	stripe_map_sector(sc, sector, &stripe, result);
	if (stripe == target_stripe)
		return;

	/* round down */
	sector = *result;
	if (sc->chunk_size_shift < 0)
		*result -= sector_div(sector, sc->chunk_size);
	else
		*result = sector & ~(sector_t)(sc->chunk_size - 1);

	if (target_stripe < stripe)
		*result += sc->chunk_size;		/* next chunk */
}

static int stripe_map_range(struct stripe_c *sc, struct bio *bio,
			    uint32_t target_stripe)
{
	sector_t begin, end;

	stripe_map_range_sector(sc, bio->bi_iter.bi_sector,
				target_stripe, &begin);
	stripe_map_range_sector(sc, bio_end_sector(bio),
				target_stripe, &end);
	if (begin < end) {
		bio_set_dev(bio, sc->stripe[target_stripe].dev->bdev);
		bio->bi_iter.bi_sector = begin +
			sc->stripe[target_stripe].physical_start;
		bio->bi_iter.bi_size = to_bytes(end - begin);
		return DM_MAPIO_REMAPPED;
	}

	/* The range doesn't map to the target stripe */
	bio_endio(bio);
	return DM_MAPIO_SUBMITTED;
}

int stripe_map(struct dm_target *ti, struct bio *bio)
{
	struct stripe_c *sc = ti->private;
	uint32_t stripe;
	unsigned int target_bio_nr;

	if (bio->bi_opf & REQ_PREFLUSH) {
		target_bio_nr = dm_bio_get_target_bio_nr(bio);
		BUG_ON(target_bio_nr >= sc->stripes);
		bio_set_dev(bio, sc->stripe[target_bio_nr].dev->bdev);
		return DM_MAPIO_REMAPPED;
	}
	if (unlikely(bio_op(bio) == REQ_OP_DISCARD) ||
	    unlikely(bio_op(bio) == REQ_OP_SECURE_ERASE) ||
	    unlikely(bio_op(bio) == REQ_OP_WRITE_ZEROES)) {
		target_bio_nr = dm_bio_get_target_bio_nr(bio);
		BUG_ON(target_bio_nr >= sc->stripes);
		return stripe_map_range(sc, bio, target_bio_nr);
	}

	stripe_map_sector(sc, bio->bi_iter.bi_sector,
			  &stripe, &bio->bi_iter.bi_sector);

	bio->bi_iter.bi_sector += sc->stripe[stripe].physical_start;
	bio_set_dev(bio, sc->stripe[stripe].dev->bdev);

	return DM_MAPIO_REMAPPED;
}

#if IS_ENABLED(CONFIG_FS_DAX)
static struct dax_device *stripe_dax_pgoff(struct dm_target *ti, pgoff_t *pgoff)
{
	struct stripe_c *sc = ti->private;
	struct block_device *bdev;
	sector_t dev_sector;
	uint32_t stripe;

	stripe_map_sector(sc, *pgoff * PAGE_SECTORS, &stripe, &dev_sector);
	dev_sector += sc->stripe[stripe].physical_start;
	bdev = sc->stripe[stripe].dev->bdev;

	*pgoff = (get_start_sect(bdev) + dev_sector) >> PAGE_SECTORS_SHIFT;
	return sc->stripe[stripe].dev->dax_dev;
}

static long stripe_dax_direct_access(struct dm_target *ti, pgoff_t pgoff,
		long nr_pages, enum dax_access_mode mode, void **kaddr,
		pfn_t *pfn)
{
	struct dax_device *dax_dev = stripe_dax_pgoff(ti, &pgoff);

	return dax_direct_access(dax_dev, pgoff, nr_pages, mode, kaddr, pfn);
}

static int stripe_dax_zero_page_range(struct dm_target *ti, pgoff_t pgoff,
				      size_t nr_pages)
{
	struct dax_device *dax_dev = stripe_dax_pgoff(ti, &pgoff);

	return dax_zero_page_range(dax_dev, pgoff, nr_pages);
}

static size_t stripe_dax_recovery_write(struct dm_target *ti, pgoff_t pgoff,
		void *addr, size_t bytes, struct iov_iter *i)
{
	struct dax_device *dax_dev = stripe_dax_pgoff(ti, &pgoff);

	return dax_recovery_write(dax_dev, pgoff, addr, bytes, i);
}

#else
#define stripe_dax_direct_access NULL
#define stripe_dax_zero_page_range NULL
#define stripe_dax_recovery_write NULL
#endif

/*
 * Stripe status:
 *
 * INFO
 * #stripes [stripe_name <stripe_name>] [group word count]
 * [error count 'A|D' <error count 'A|D'>]
 *
 * TABLE
 * #stripes [stripe chunk size]
 * [stripe_name physical_start <stripe_name physical_start>]
 *
 */

static void stripe_status(struct dm_target *ti, status_type_t type,
			  unsigned int status_flags, char *result, unsigned int maxlen)
{
	struct stripe_c *sc = ti->private;
	unsigned int sz = 0;
	unsigned int i;

	switch (type) {
	case STATUSTYPE_INFO:
		DMEMIT("%d ", sc->stripes);
		for (i = 0; i < sc->stripes; i++)
			DMEMIT("%s ", sc->stripe[i].dev->name);

		DMEMIT("1 ");
		for (i = 0; i < sc->stripes; i++)
			DMEMIT("%c", atomic_read(&(sc->stripe[i].error_count)) ?  'D' : 'A');
		break;

	case STATUSTYPE_TABLE:
		DMEMIT("%d %llu", sc->stripes,
			(unsigned long long)sc->chunk_size);
		for (i = 0; i < sc->stripes; i++)
			DMEMIT(" %s %llu", sc->stripe[i].dev->name,
			    (unsigned long long)sc->stripe[i].physical_start);
		break;

	case STATUSTYPE_IMA:
		DMEMIT_TARGET_NAME_VERSION(ti->type);
		DMEMIT(",stripes=%d,chunk_size=%llu", sc->stripes,
		       (unsigned long long)sc->chunk_size);

		for (i = 0; i < sc->stripes; i++) {
			DMEMIT(",stripe_%d_device_name=%s", i, sc->stripe[i].dev->name);
			DMEMIT(",stripe_%d_physical_start=%llu", i,
			       (unsigned long long)sc->stripe[i].physical_start);
			DMEMIT(",stripe_%d_status=%c", i,
			       atomic_read(&(sc->stripe[i].error_count)) ? 'D' : 'A');
		}
		DMEMIT(";");
		break;
	}
}

static int stripe_end_io(struct dm_target *ti, struct bio *bio,
		blk_status_t *error)
{
	unsigned int i;
	char major_minor[16];
	struct stripe_c *sc = ti->private;

	if (!*error)
		return DM_ENDIO_DONE; /* I/O complete */

	if (bio->bi_opf & REQ_RAHEAD)
		return DM_ENDIO_DONE;

	if (*error == BLK_STS_NOTSUPP)
		return DM_ENDIO_DONE;

	memset(major_minor, 0, sizeof(major_minor));
	sprintf(major_minor, "%d:%d", MAJOR(bio_dev(bio)), MINOR(bio_dev(bio)));

	/*
	 * Test to see which stripe drive triggered the event
	 * and increment error count for all stripes on that device.
	 * If the error count for a given device exceeds the threshold
	 * value we will no longer trigger any further events.
	 */
	for (i = 0; i < sc->stripes; i++)
		if (!strcmp(sc->stripe[i].dev->name, major_minor)) {
			atomic_inc(&(sc->stripe[i].error_count));
			if (atomic_read(&(sc->stripe[i].error_count)) <
			    DM_IO_ERROR_THRESHOLD)
				queue_work(dm_stripe_wq, &sc->trigger_event);
		}

	return DM_ENDIO_DONE;
}

static int stripe_iterate_devices(struct dm_target *ti,
				  iterate_devices_callout_fn fn, void *data)
{
	struct stripe_c *sc = ti->private;
	int ret = 0;
	unsigned int i = 0;

	do {
		ret = fn(ti, sc->stripe[i].dev,
			 sc->stripe[i].physical_start,
			 sc->stripe_width, data);
	} while (!ret && ++i < sc->stripes);

	return ret;
}

static void stripe_io_hints(struct dm_target *ti,
			    struct queue_limits *limits)
{
	struct stripe_c *sc = ti->private;
	unsigned int chunk_size = sc->chunk_size << SECTOR_SHIFT;

	blk_limits_io_min(limits, chunk_size);
	blk_limits_io_opt(limits, chunk_size * sc->stripes);
}

static struct target_type stripe_target = {
	.name   = "striped",
	.version = {1, 6, 0},
	.features = DM_TARGET_PASSES_INTEGRITY | DM_TARGET_NOWAIT,
	.module = THIS_MODULE,
	.ctr    = stripe_ctr,
	.dtr    = stripe_dtr,
	.map    = stripe_map,
	.end_io = stripe_end_io,
	.status = stripe_status,
	.iterate_devices = stripe_iterate_devices,
	.io_hints = stripe_io_hints,
	.direct_access = stripe_dax_direct_access,
	.dax_zero_page_range = stripe_dax_zero_page_range,
	.dax_recovery_write = stripe_dax_recovery_write,
};

int __init dm_stripe_init(void)
{
	int r;

	dm_stripe_wq = alloc_workqueue("dm_stripe_wq", 0, 0);
	if (!dm_stripe_wq)
		return -ENOMEM;
	r = dm_register_target(&stripe_target);
	if (r < 0) {
		destroy_workqueue(dm_stripe_wq);
		DMWARN("target registration failed");
	}

	return r;
}

void dm_stripe_exit(void)
{
	dm_unregister_target(&stripe_target);
	destroy_workqueue(dm_stripe_wq);
}
