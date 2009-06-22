/*
 * Copyright (C) 2001-2003 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#include <linux/device-mapper.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/log2.h>

#define DM_MSG_PREFIX "striped"
#define DM_IO_ERROR_THRESHOLD 15

struct stripe {
	struct dm_dev *dev;
	sector_t physical_start;

	atomic_t error_count;
};

struct stripe_c {
	uint32_t stripes;

	/* The size of this target / num. stripes */
	sector_t stripe_width;

	/* stripe chunk size */
	uint32_t chunk_shift;
	sector_t chunk_mask;

	/* Needed for handling events */
	struct dm_target *ti;

	/* Work struct used for triggering events*/
	struct work_struct kstriped_ws;

	struct stripe stripe[0];
};

static struct workqueue_struct *kstriped;

/*
 * An event is triggered whenever a drive
 * drops out of a stripe volume.
 */
static void trigger_event(struct work_struct *work)
{
	struct stripe_c *sc = container_of(work, struct stripe_c, kstriped_ws);

	dm_table_event(sc->ti->table);

}

static inline struct stripe_c *alloc_context(unsigned int stripes)
{
	size_t len;

	if (dm_array_too_big(sizeof(struct stripe_c), sizeof(struct stripe),
			     stripes))
		return NULL;

	len = sizeof(struct stripe_c) + (sizeof(struct stripe) * stripes);

	return kmalloc(len, GFP_KERNEL);
}

/*
 * Parse a single <dev> <sector> pair
 */
static int get_stripe(struct dm_target *ti, struct stripe_c *sc,
		      unsigned int stripe, char **argv)
{
	unsigned long long start;

	if (sscanf(argv[1], "%llu", &start) != 1)
		return -EINVAL;

	if (dm_get_device(ti, argv[0], start, sc->stripe_width,
			  dm_table_get_mode(ti->table),
			  &sc->stripe[stripe].dev))
		return -ENXIO;

	sc->stripe[stripe].physical_start = start;

	return 0;
}

/*
 * Construct a striped mapping.
 * <number of stripes> <chunk size (2^^n)> [<dev_path> <offset>]+
 */
static int stripe_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct stripe_c *sc;
	sector_t width;
	uint32_t stripes;
	uint32_t chunk_size;
	char *end;
	int r;
	unsigned int i;

	if (argc < 2) {
		ti->error = "Not enough arguments";
		return -EINVAL;
	}

	stripes = simple_strtoul(argv[0], &end, 10);
	if (*end) {
		ti->error = "Invalid stripe count";
		return -EINVAL;
	}

	chunk_size = simple_strtoul(argv[1], &end, 10);
	if (*end) {
		ti->error = "Invalid chunk_size";
		return -EINVAL;
	}

	/*
	 * chunk_size is a power of two
	 */
	if (!is_power_of_2(chunk_size) ||
	    (chunk_size < (PAGE_SIZE >> SECTOR_SHIFT))) {
		ti->error = "Invalid chunk size";
		return -EINVAL;
	}

	if (ti->len & (chunk_size - 1)) {
		ti->error = "Target length not divisible by "
		    "chunk size";
		return -EINVAL;
	}

	width = ti->len;
	if (sector_div(width, stripes)) {
		ti->error = "Target length not divisible by "
		    "number of stripes";
		return -EINVAL;
	}

	/*
	 * Do we have enough arguments for that many stripes ?
	 */
	if (argc != (2 + 2 * stripes)) {
		ti->error = "Not enough destinations "
			"specified";
		return -EINVAL;
	}

	sc = alloc_context(stripes);
	if (!sc) {
		ti->error = "Memory allocation for striped context "
		    "failed";
		return -ENOMEM;
	}

	INIT_WORK(&sc->kstriped_ws, trigger_event);

	/* Set pointer to dm target; used in trigger_event */
	sc->ti = ti;

	sc->stripes = stripes;
	sc->stripe_width = width;
	ti->split_io = chunk_size;
	ti->num_flush_requests = stripes;

	sc->chunk_mask = ((sector_t) chunk_size) - 1;
	for (sc->chunk_shift = 0; chunk_size; sc->chunk_shift++)
		chunk_size >>= 1;
	sc->chunk_shift--;

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
	struct stripe_c *sc = (struct stripe_c *) ti->private;

	for (i = 0; i < sc->stripes; i++)
		dm_put_device(ti, sc->stripe[i].dev);

	flush_workqueue(kstriped);
	kfree(sc);
}

static int stripe_map(struct dm_target *ti, struct bio *bio,
		      union map_info *map_context)
{
	struct stripe_c *sc = (struct stripe_c *) ti->private;
	sector_t offset, chunk;
	uint32_t stripe;

	if (unlikely(bio_empty_barrier(bio))) {
		BUG_ON(map_context->flush_request >= sc->stripes);
		bio->bi_bdev = sc->stripe[map_context->flush_request].dev->bdev;
		return DM_MAPIO_REMAPPED;
	}

	offset = bio->bi_sector - ti->begin;
	chunk = offset >> sc->chunk_shift;
	stripe = sector_div(chunk, sc->stripes);

	bio->bi_bdev = sc->stripe[stripe].dev->bdev;
	bio->bi_sector = sc->stripe[stripe].physical_start +
	    (chunk << sc->chunk_shift) + (offset & sc->chunk_mask);
	return DM_MAPIO_REMAPPED;
}

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

static int stripe_status(struct dm_target *ti,
			 status_type_t type, char *result, unsigned int maxlen)
{
	struct stripe_c *sc = (struct stripe_c *) ti->private;
	char buffer[sc->stripes + 1];
	unsigned int sz = 0;
	unsigned int i;

	switch (type) {
	case STATUSTYPE_INFO:
		DMEMIT("%d ", sc->stripes);
		for (i = 0; i < sc->stripes; i++)  {
			DMEMIT("%s ", sc->stripe[i].dev->name);
			buffer[i] = atomic_read(&(sc->stripe[i].error_count)) ?
				'D' : 'A';
		}
		buffer[i] = '\0';
		DMEMIT("1 %s", buffer);
		break;

	case STATUSTYPE_TABLE:
		DMEMIT("%d %llu", sc->stripes,
			(unsigned long long)sc->chunk_mask + 1);
		for (i = 0; i < sc->stripes; i++)
			DMEMIT(" %s %llu", sc->stripe[i].dev->name,
			    (unsigned long long)sc->stripe[i].physical_start);
		break;
	}
	return 0;
}

static int stripe_end_io(struct dm_target *ti, struct bio *bio,
			 int error, union map_info *map_context)
{
	unsigned i;
	char major_minor[16];
	struct stripe_c *sc = ti->private;

	if (!error)
		return 0; /* I/O complete */

	if ((error == -EWOULDBLOCK) && bio_rw_ahead(bio))
		return error;

	if (error == -EOPNOTSUPP)
		return error;

	memset(major_minor, 0, sizeof(major_minor));
	sprintf(major_minor, "%d:%d",
		MAJOR(disk_devt(bio->bi_bdev->bd_disk)),
		MINOR(disk_devt(bio->bi_bdev->bd_disk)));

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
				queue_work(kstriped, &sc->kstriped_ws);
		}

	return error;
}

static struct target_type stripe_target = {
	.name   = "striped",
	.version = {1, 1, 0},
	.module = THIS_MODULE,
	.ctr    = stripe_ctr,
	.dtr    = stripe_dtr,
	.map    = stripe_map,
	.end_io = stripe_end_io,
	.status = stripe_status,
};

int __init dm_stripe_init(void)
{
	int r;

	r = dm_register_target(&stripe_target);
	if (r < 0) {
		DMWARN("target registration failed");
		return r;
	}

	kstriped = create_singlethread_workqueue("kstriped");
	if (!kstriped) {
		DMERR("failed to create workqueue kstriped");
		dm_unregister_target(&stripe_target);
		return -ENOMEM;
	}

	return r;
}

void dm_stripe_exit(void)
{
	dm_unregister_target(&stripe_target);
	destroy_workqueue(kstriped);

	return;
}
