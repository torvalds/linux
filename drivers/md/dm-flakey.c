/*
 * Copyright (C) 2003 Sistina Software (UK) Limited.
 * Copyright (C) 2004, 2010-2011 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include <linux/device-mapper.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/slab.h>

#define DM_MSG_PREFIX "flakey"

#define all_corrupt_bio_flags_match(bio, fc)	\
	(((bio)->bi_opf & (fc)->corrupt_bio_flags) == (fc)->corrupt_bio_flags)

/*
 * Flakey: Used for testing only, simulates intermittent,
 * catastrophic device failure.
 */
struct flakey_c {
	struct dm_dev *dev;
	unsigned long start_time;
	sector_t start;
	unsigned up_interval;
	unsigned down_interval;
	unsigned long flags;
	unsigned corrupt_bio_byte;
	unsigned corrupt_bio_rw;
	unsigned corrupt_bio_value;
	blk_opf_t corrupt_bio_flags;
};

enum feature_flag_bits {
	DROP_WRITES,
	ERROR_WRITES
};

struct per_bio_data {
	bool bio_submitted;
};

static int parse_features(struct dm_arg_set *as, struct flakey_c *fc,
			  struct dm_target *ti)
{
	int r;
	unsigned argc;
	const char *arg_name;

	static const struct dm_arg _args[] = {
		{0, 6, "Invalid number of feature args"},
		{1, UINT_MAX, "Invalid corrupt bio byte"},
		{0, 255, "Invalid corrupt value to write into bio byte (0-255)"},
		{0, UINT_MAX, "Invalid corrupt bio flags mask"},
	};

	/* No feature arguments supplied. */
	if (!as->argc)
		return 0;

	r = dm_read_arg_group(_args, as, &argc, &ti->error);
	if (r)
		return r;

	while (argc) {
		arg_name = dm_shift_arg(as);
		argc--;

		if (!arg_name) {
			ti->error = "Insufficient feature arguments";
			return -EINVAL;
		}

		/*
		 * drop_writes
		 */
		if (!strcasecmp(arg_name, "drop_writes")) {
			if (test_and_set_bit(DROP_WRITES, &fc->flags)) {
				ti->error = "Feature drop_writes duplicated";
				return -EINVAL;
			} else if (test_bit(ERROR_WRITES, &fc->flags)) {
				ti->error = "Feature drop_writes conflicts with feature error_writes";
				return -EINVAL;
			}

			continue;
		}

		/*
		 * error_writes
		 */
		if (!strcasecmp(arg_name, "error_writes")) {
			if (test_and_set_bit(ERROR_WRITES, &fc->flags)) {
				ti->error = "Feature error_writes duplicated";
				return -EINVAL;

			} else if (test_bit(DROP_WRITES, &fc->flags)) {
				ti->error = "Feature error_writes conflicts with feature drop_writes";
				return -EINVAL;
			}

			continue;
		}

		/*
		 * corrupt_bio_byte <Nth_byte> <direction> <value> <bio_flags>
		 */
		if (!strcasecmp(arg_name, "corrupt_bio_byte")) {
			if (!argc) {
				ti->error = "Feature corrupt_bio_byte requires parameters";
				return -EINVAL;
			}

			r = dm_read_arg(_args + 1, as, &fc->corrupt_bio_byte, &ti->error);
			if (r)
				return r;
			argc--;

			/*
			 * Direction r or w?
			 */
			arg_name = dm_shift_arg(as);
			if (!strcasecmp(arg_name, "w"))
				fc->corrupt_bio_rw = WRITE;
			else if (!strcasecmp(arg_name, "r"))
				fc->corrupt_bio_rw = READ;
			else {
				ti->error = "Invalid corrupt bio direction (r or w)";
				return -EINVAL;
			}
			argc--;

			/*
			 * Value of byte (0-255) to write in place of correct one.
			 */
			r = dm_read_arg(_args + 2, as, &fc->corrupt_bio_value, &ti->error);
			if (r)
				return r;
			argc--;

			/*
			 * Only corrupt bios with these flags set.
			 */
			BUILD_BUG_ON(sizeof(fc->corrupt_bio_flags) !=
				     sizeof(unsigned int));
			r = dm_read_arg(_args + 3, as,
				(__force unsigned *)&fc->corrupt_bio_flags,
				&ti->error);
			if (r)
				return r;
			argc--;

			continue;
		}

		ti->error = "Unrecognised flakey feature requested";
		return -EINVAL;
	}

	if (test_bit(DROP_WRITES, &fc->flags) && (fc->corrupt_bio_rw == WRITE)) {
		ti->error = "drop_writes is incompatible with corrupt_bio_byte with the WRITE flag set";
		return -EINVAL;

	} else if (test_bit(ERROR_WRITES, &fc->flags) && (fc->corrupt_bio_rw == WRITE)) {
		ti->error = "error_writes is incompatible with corrupt_bio_byte with the WRITE flag set";
		return -EINVAL;
	}

	return 0;
}

/*
 * Construct a flakey mapping:
 * <dev_path> <offset> <up interval> <down interval> [<#feature args> [<arg>]*]
 *
 *   Feature args:
 *     [drop_writes]
 *     [corrupt_bio_byte <Nth_byte> <direction> <value> <bio_flags>]
 *
 *   Nth_byte starts from 1 for the first byte.
 *   Direction is r for READ or w for WRITE.
 *   bio_flags is ignored if 0.
 */
static int flakey_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	static const struct dm_arg _args[] = {
		{0, UINT_MAX, "Invalid up interval"},
		{0, UINT_MAX, "Invalid down interval"},
	};

	int r;
	struct flakey_c *fc;
	unsigned long long tmpll;
	struct dm_arg_set as;
	const char *devname;
	char dummy;

	as.argc = argc;
	as.argv = argv;

	if (argc < 4) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	fc = kzalloc(sizeof(*fc), GFP_KERNEL);
	if (!fc) {
		ti->error = "Cannot allocate context";
		return -ENOMEM;
	}
	fc->start_time = jiffies;

	devname = dm_shift_arg(&as);

	r = -EINVAL;
	if (sscanf(dm_shift_arg(&as), "%llu%c", &tmpll, &dummy) != 1 || tmpll != (sector_t)tmpll) {
		ti->error = "Invalid device sector";
		goto bad;
	}
	fc->start = tmpll;

	r = dm_read_arg(_args, &as, &fc->up_interval, &ti->error);
	if (r)
		goto bad;

	r = dm_read_arg(_args, &as, &fc->down_interval, &ti->error);
	if (r)
		goto bad;

	if (!(fc->up_interval + fc->down_interval)) {
		ti->error = "Total (up + down) interval is zero";
		r = -EINVAL;
		goto bad;
	}

	if (fc->up_interval + fc->down_interval < fc->up_interval) {
		ti->error = "Interval overflow";
		r = -EINVAL;
		goto bad;
	}

	r = parse_features(&as, fc, ti);
	if (r)
		goto bad;

	r = dm_get_device(ti, devname, dm_table_get_mode(ti->table), &fc->dev);
	if (r) {
		ti->error = "Device lookup failed";
		goto bad;
	}

	ti->num_flush_bios = 1;
	ti->num_discard_bios = 1;
	ti->per_io_data_size = sizeof(struct per_bio_data);
	ti->private = fc;
	return 0;

bad:
	kfree(fc);
	return r;
}

static void flakey_dtr(struct dm_target *ti)
{
	struct flakey_c *fc = ti->private;

	dm_put_device(ti, fc->dev);
	kfree(fc);
}

static sector_t flakey_map_sector(struct dm_target *ti, sector_t bi_sector)
{
	struct flakey_c *fc = ti->private;

	return fc->start + dm_target_offset(ti, bi_sector);
}

static void flakey_map_bio(struct dm_target *ti, struct bio *bio)
{
	struct flakey_c *fc = ti->private;

	bio_set_dev(bio, fc->dev->bdev);
	bio->bi_iter.bi_sector = flakey_map_sector(ti, bio->bi_iter.bi_sector);
}

static void corrupt_bio_data(struct bio *bio, struct flakey_c *fc)
{
	unsigned int corrupt_bio_byte = fc->corrupt_bio_byte - 1;

	struct bvec_iter iter;
	struct bio_vec bvec;

	if (!bio_has_data(bio))
		return;

	/*
	 * Overwrite the Nth byte of the bio's data, on whichever page
	 * it falls.
	 */
	bio_for_each_segment(bvec, bio, iter) {
		if (bio_iter_len(bio, iter) > corrupt_bio_byte) {
			char *segment;
			struct page *page = bio_iter_page(bio, iter);
			if (unlikely(page == ZERO_PAGE(0)))
				break;
			segment = bvec_kmap_local(&bvec);
			segment[corrupt_bio_byte] = fc->corrupt_bio_value;
			kunmap_local(segment);
			DMDEBUG("Corrupting data bio=%p by writing %u to byte %u "
				"(rw=%c bi_opf=%u bi_sector=%llu size=%u)\n",
				bio, fc->corrupt_bio_value, fc->corrupt_bio_byte,
				(bio_data_dir(bio) == WRITE) ? 'w' : 'r', bio->bi_opf,
				(unsigned long long)bio->bi_iter.bi_sector, bio->bi_iter.bi_size);
			break;
		}
		corrupt_bio_byte -= bio_iter_len(bio, iter);
	}
}

static int flakey_map(struct dm_target *ti, struct bio *bio)
{
	struct flakey_c *fc = ti->private;
	unsigned elapsed;
	struct per_bio_data *pb = dm_per_bio_data(bio, sizeof(struct per_bio_data));
	pb->bio_submitted = false;

	if (op_is_zone_mgmt(bio_op(bio)))
		goto map_bio;

	/* Are we alive ? */
	elapsed = (jiffies - fc->start_time) / HZ;
	if (elapsed % (fc->up_interval + fc->down_interval) >= fc->up_interval) {
		/*
		 * Flag this bio as submitted while down.
		 */
		pb->bio_submitted = true;

		/*
		 * Error reads if neither corrupt_bio_byte or drop_writes or error_writes are set.
		 * Otherwise, flakey_end_io() will decide if the reads should be modified.
		 */
		if (bio_data_dir(bio) == READ) {
			if (!fc->corrupt_bio_byte && !test_bit(DROP_WRITES, &fc->flags) &&
			    !test_bit(ERROR_WRITES, &fc->flags))
				return DM_MAPIO_KILL;
			goto map_bio;
		}

		/*
		 * Drop or error writes?
		 */
		if (test_bit(DROP_WRITES, &fc->flags)) {
			bio_endio(bio);
			return DM_MAPIO_SUBMITTED;
		}
		else if (test_bit(ERROR_WRITES, &fc->flags)) {
			bio_io_error(bio);
			return DM_MAPIO_SUBMITTED;
		}

		/*
		 * Corrupt matching writes.
		 */
		if (fc->corrupt_bio_byte) {
			if (fc->corrupt_bio_rw == WRITE) {
				if (all_corrupt_bio_flags_match(bio, fc))
					corrupt_bio_data(bio, fc);
			}
			goto map_bio;
		}

		/*
		 * By default, error all I/O.
		 */
		return DM_MAPIO_KILL;
	}

map_bio:
	flakey_map_bio(ti, bio);

	return DM_MAPIO_REMAPPED;
}

static int flakey_end_io(struct dm_target *ti, struct bio *bio,
			 blk_status_t *error)
{
	struct flakey_c *fc = ti->private;
	struct per_bio_data *pb = dm_per_bio_data(bio, sizeof(struct per_bio_data));

	if (op_is_zone_mgmt(bio_op(bio)))
		return DM_ENDIO_DONE;

	if (!*error && pb->bio_submitted && (bio_data_dir(bio) == READ)) {
		if (fc->corrupt_bio_byte) {
			if ((fc->corrupt_bio_rw == READ) &&
			    all_corrupt_bio_flags_match(bio, fc)) {
				/*
				 * Corrupt successful matching READs while in down state.
				 */
				corrupt_bio_data(bio, fc);
			}
		} else if (!test_bit(DROP_WRITES, &fc->flags) &&
			   !test_bit(ERROR_WRITES, &fc->flags)) {
			/*
			 * Error read during the down_interval if drop_writes
			 * and error_writes were not configured.
			 */
			*error = BLK_STS_IOERR;
		}
	}

	return DM_ENDIO_DONE;
}

static void flakey_status(struct dm_target *ti, status_type_t type,
			  unsigned status_flags, char *result, unsigned maxlen)
{
	unsigned sz = 0;
	struct flakey_c *fc = ti->private;
	unsigned drop_writes, error_writes;

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		DMEMIT("%s %llu %u %u ", fc->dev->name,
		       (unsigned long long)fc->start, fc->up_interval,
		       fc->down_interval);

		drop_writes = test_bit(DROP_WRITES, &fc->flags);
		error_writes = test_bit(ERROR_WRITES, &fc->flags);
		DMEMIT("%u ", drop_writes + error_writes + (fc->corrupt_bio_byte > 0) * 5);

		if (drop_writes)
			DMEMIT("drop_writes ");
		else if (error_writes)
			DMEMIT("error_writes ");

		if (fc->corrupt_bio_byte)
			DMEMIT("corrupt_bio_byte %u %c %u %u ",
			       fc->corrupt_bio_byte,
			       (fc->corrupt_bio_rw == WRITE) ? 'w' : 'r',
			       fc->corrupt_bio_value, fc->corrupt_bio_flags);

		break;

	case STATUSTYPE_IMA:
		result[0] = '\0';
		break;
	}
}

static int flakey_prepare_ioctl(struct dm_target *ti, struct block_device **bdev)
{
	struct flakey_c *fc = ti->private;

	*bdev = fc->dev->bdev;

	/*
	 * Only pass ioctls through if the device sizes match exactly.
	 */
	if (fc->start || ti->len != bdev_nr_sectors((*bdev)))
		return 1;
	return 0;
}

#ifdef CONFIG_BLK_DEV_ZONED
static int flakey_report_zones(struct dm_target *ti,
		struct dm_report_zones_args *args, unsigned int nr_zones)
{
	struct flakey_c *fc = ti->private;

	return dm_report_zones(fc->dev->bdev, fc->start,
			       flakey_map_sector(ti, args->next_sector),
			       args, nr_zones);
}
#else
#define flakey_report_zones NULL
#endif

static int flakey_iterate_devices(struct dm_target *ti, iterate_devices_callout_fn fn, void *data)
{
	struct flakey_c *fc = ti->private;

	return fn(ti, fc->dev, fc->start, ti->len, data);
}

static struct target_type flakey_target = {
	.name   = "flakey",
	.version = {1, 5, 0},
	.features = DM_TARGET_ZONED_HM | DM_TARGET_PASSES_CRYPTO,
	.report_zones = flakey_report_zones,
	.module = THIS_MODULE,
	.ctr    = flakey_ctr,
	.dtr    = flakey_dtr,
	.map    = flakey_map,
	.end_io = flakey_end_io,
	.status = flakey_status,
	.prepare_ioctl = flakey_prepare_ioctl,
	.iterate_devices = flakey_iterate_devices,
};

static int __init dm_flakey_init(void)
{
	int r = dm_register_target(&flakey_target);

	if (r < 0)
		DMERR("register failed %d", r);

	return r;
}

static void __exit dm_flakey_exit(void)
{
	dm_unregister_target(&flakey_target);
}

/* Module hooks */
module_init(dm_flakey_init);
module_exit(dm_flakey_exit);

MODULE_DESCRIPTION(DM_NAME " flakey target");
MODULE_AUTHOR("Joe Thornber <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
