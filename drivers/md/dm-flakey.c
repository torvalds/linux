// SPDX-License-Identifier: GPL-2.0-only
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

#define PROBABILITY_BASE	1000000000

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
	unsigned int up_interval;
	unsigned int down_interval;
	unsigned long flags;
	unsigned int corrupt_bio_byte;
	unsigned int corrupt_bio_rw;
	unsigned int corrupt_bio_value;
	blk_opf_t corrupt_bio_flags;
	unsigned int random_read_corrupt;
	unsigned int random_write_corrupt;
};

enum feature_flag_bits {
	ERROR_READS,
	DROP_WRITES,
	ERROR_WRITES
};

struct per_bio_data {
	bool bio_can_corrupt;
	struct bvec_iter saved_iter;
};

static int parse_features(struct dm_arg_set *as, struct flakey_c *fc,
			  struct dm_target *ti)
{
	int r = 0;
	unsigned int argc = 0;
	const char *arg_name;

	static const struct dm_arg _args[] = {
		{0, 11, "Invalid number of feature args"},
		{1, UINT_MAX, "Invalid corrupt bio byte"},
		{0, 255, "Invalid corrupt value to write into bio byte (0-255)"},
		{0, UINT_MAX, "Invalid corrupt bio flags mask"},
		{0, PROBABILITY_BASE, "Invalid random corrupt argument"},
	};

	if (as->argc && (r = dm_read_arg_group(_args, as, &argc, &ti->error)))
		return r;

	/* No feature arguments supplied. */
	if (!argc)
		goto error_all_io;

	while (argc) {
		arg_name = dm_shift_arg(as);
		argc--;

		if (!arg_name) {
			ti->error = "Insufficient feature arguments";
			return -EINVAL;
		}

		/*
		 * error_reads
		 */
		if (!strcasecmp(arg_name, "error_reads")) {
			if (test_and_set_bit(ERROR_READS, &fc->flags)) {
				ti->error = "Feature error_reads duplicated";
				return -EINVAL;
			}
			continue;
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
			if (fc->corrupt_bio_byte) {
				ti->error = "Feature corrupt_bio_byte duplicated";
				return -EINVAL;
			} else if (argc < 4) {
				ti->error = "Feature corrupt_bio_byte requires 4 parameters";
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
			if (arg_name && !strcasecmp(arg_name, "w"))
				fc->corrupt_bio_rw = WRITE;
			else if (arg_name && !strcasecmp(arg_name, "r"))
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
				(__force unsigned int *)&fc->corrupt_bio_flags,
				&ti->error);
			if (r)
				return r;
			argc--;

			continue;
		}

		if (!strcasecmp(arg_name, "random_read_corrupt")) {
			if (fc->random_read_corrupt) {
				ti->error = "Feature random_read_corrupt duplicated";
				return -EINVAL;
			} else if (!argc) {
				ti->error = "Feature random_read_corrupt requires a parameter";
				return -EINVAL;
			}
			r = dm_read_arg(_args + 4, as, &fc->random_read_corrupt, &ti->error);
			if (r)
				return r;
			argc--;

			continue;
		}

		if (!strcasecmp(arg_name, "random_write_corrupt")) {
			if (fc->random_write_corrupt) {
				ti->error = "Feature random_write_corrupt duplicated";
				return -EINVAL;
			} else if (!argc) {
				ti->error = "Feature random_write_corrupt requires a parameter";
				return -EINVAL;
			}
			r = dm_read_arg(_args + 4, as, &fc->random_write_corrupt, &ti->error);
			if (r)
				return r;
			argc--;

			continue;
		}

		ti->error = "Unrecognised flakey feature requested";
		return -EINVAL;
	}

	if (test_bit(DROP_WRITES, &fc->flags) &&
	    ((fc->corrupt_bio_byte && fc->corrupt_bio_rw == WRITE) ||
	     fc->random_write_corrupt)) {
		ti->error = "drop_writes is incompatible with random_write_corrupt or corrupt_bio_byte with the WRITE flag set";
		return -EINVAL;

	} else if (test_bit(ERROR_WRITES, &fc->flags) &&
		   ((fc->corrupt_bio_byte && fc->corrupt_bio_rw == WRITE) ||
		    fc->random_write_corrupt)) {
		ti->error = "error_writes is incompatible with random_write_corrupt or corrupt_bio_byte with the WRITE flag set";
		return -EINVAL;
	} else if (test_bit(ERROR_READS, &fc->flags) &&
		   ((fc->corrupt_bio_byte && fc->corrupt_bio_rw == READ) ||
		    fc->random_read_corrupt)) {
		ti->error = "error_reads is incompatible with random_read_corrupt or corrupt_bio_byte with the READ flag set";
		return -EINVAL;
	}

	if (!fc->corrupt_bio_byte && !test_bit(ERROR_READS, &fc->flags) &&
	    !test_bit(DROP_WRITES, &fc->flags) && !test_bit(ERROR_WRITES, &fc->flags) &&
	    !fc->random_read_corrupt && !fc->random_write_corrupt) {
error_all_io:
		set_bit(ERROR_WRITES, &fc->flags);
		set_bit(ERROR_READS, &fc->flags);
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

	r = dm_read_arg(_args + 1, &as, &fc->down_interval, &ti->error);
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

static void corrupt_bio_common(struct bio *bio, unsigned int corrupt_bio_byte,
			       unsigned char corrupt_bio_value,
			       struct bvec_iter start)
{
	struct bvec_iter iter;
	struct bio_vec bvec;

	/*
	 * Overwrite the Nth byte of the bio's data, on whichever page
	 * it falls.
	 */
	__bio_for_each_segment(bvec, bio, iter, start) {
		if (bio_iter_len(bio, iter) > corrupt_bio_byte) {
			unsigned char *segment = bvec_kmap_local(&bvec);
			segment[corrupt_bio_byte] = corrupt_bio_value;
			kunmap_local(segment);
			DMDEBUG("Corrupting data bio=%p by writing %u to byte %u "
				"(rw=%c bi_opf=%u bi_sector=%llu size=%u)\n",
				bio, corrupt_bio_value, corrupt_bio_byte,
				(bio_data_dir(bio) == WRITE) ? 'w' : 'r', bio->bi_opf,
				(unsigned long long)start.bi_sector,
				start.bi_size);
			break;
		}
		corrupt_bio_byte -= bio_iter_len(bio, iter);
	}
}

static void corrupt_bio_data(struct bio *bio, struct flakey_c *fc,
			     struct bvec_iter start)
{
	unsigned int corrupt_bio_byte = fc->corrupt_bio_byte - 1;

	corrupt_bio_common(bio, corrupt_bio_byte, fc->corrupt_bio_value, start);
}

static void corrupt_bio_random(struct bio *bio, struct bvec_iter start)
{
	unsigned int corrupt_byte;
	unsigned char corrupt_value;

	corrupt_byte = get_random_u32() % start.bi_size;
	corrupt_value = get_random_u8();

	corrupt_bio_common(bio, corrupt_byte, corrupt_value, start);
}

static void clone_free(struct bio *clone)
{
	struct folio_iter fi;

	if (clone->bi_vcnt > 0) { /* bio_for_each_folio_all crashes with an empty bio */
		bio_for_each_folio_all(fi, clone)
			folio_put(fi.folio);
	}

	bio_uninit(clone);
	kfree(clone);
}

static void clone_endio(struct bio *clone)
{
	struct bio *bio = clone->bi_private;
	bio->bi_status = clone->bi_status;
	clone_free(clone);
	bio_endio(bio);
}

static struct bio *clone_bio(struct dm_target *ti, struct flakey_c *fc, struct bio *bio)
{
	struct bio *clone;
	unsigned size, remaining_size, nr_iovecs, order;
	struct bvec_iter iter = bio->bi_iter;

	if (unlikely(bio->bi_iter.bi_size > UIO_MAXIOV << PAGE_SHIFT))
		dm_accept_partial_bio(bio, UIO_MAXIOV << PAGE_SHIFT >> SECTOR_SHIFT);

	size = bio->bi_iter.bi_size;
	nr_iovecs = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	clone = bio_kmalloc(nr_iovecs, GFP_NOIO | __GFP_NORETRY | __GFP_NOWARN);
	if (!clone)
		return NULL;

	bio_init_inline(clone, fc->dev->bdev, nr_iovecs, bio->bi_opf);

	clone->bi_iter.bi_sector = flakey_map_sector(ti, bio->bi_iter.bi_sector);
	clone->bi_private = bio;
	clone->bi_end_io = clone_endio;

	remaining_size = size;

	order = MAX_PAGE_ORDER;
	while (remaining_size) {
		struct page *pages;
		unsigned size_to_add, to_copy;
		unsigned char *virt;
		unsigned remaining_order = __fls((remaining_size + PAGE_SIZE - 1) >> PAGE_SHIFT);
		order = min(order, remaining_order);

retry_alloc_pages:
		pages = alloc_pages(GFP_NOIO | __GFP_NORETRY | __GFP_NOWARN | __GFP_COMP, order);
		if (unlikely(!pages)) {
			if (order) {
				order--;
				goto retry_alloc_pages;
			}
			clone_free(clone);
			return NULL;
		}
		size_to_add = min((unsigned)PAGE_SIZE << order, remaining_size);

		virt = page_to_virt(pages);
		to_copy = size_to_add;
		do {
			struct bio_vec bvec = bvec_iter_bvec(bio->bi_io_vec, iter);
			unsigned this_step = min(bvec.bv_len, to_copy);
			void *map = bvec_kmap_local(&bvec);
			memcpy(virt, map, this_step);
			kunmap_local(map);

			bvec_iter_advance(bio->bi_io_vec, &iter, this_step);
			to_copy -= this_step;
			virt += this_step;
		} while (to_copy);

		__bio_add_page(clone, pages, size_to_add, 0);
		remaining_size -= size_to_add;
	}

	return clone;
}

static int flakey_map(struct dm_target *ti, struct bio *bio)
{
	struct flakey_c *fc = ti->private;
	unsigned int elapsed;
	struct per_bio_data *pb = dm_per_bio_data(bio, sizeof(struct per_bio_data));

	pb->bio_can_corrupt = false;

	if (op_is_zone_mgmt(bio_op(bio)))
		goto map_bio;

	/* Are we alive ? */
	elapsed = (jiffies - fc->start_time) / HZ;
	if (elapsed % (fc->up_interval + fc->down_interval) >= fc->up_interval) {
		bool corrupt_fixed, corrupt_random;

		if (bio_has_data(bio)) {
			pb->bio_can_corrupt = true;
			pb->saved_iter = bio->bi_iter;
		}

		/*
		 * If ERROR_READS isn't set flakey_end_io() will decide if the
		 * reads should be modified.
		 */
		if (bio_data_dir(bio) == READ) {
			if (test_bit(ERROR_READS, &fc->flags))
				return DM_MAPIO_KILL;
			goto map_bio;
		}

		/*
		 * Drop or error writes?
		 */
		if (test_bit(DROP_WRITES, &fc->flags)) {
			bio_endio(bio);
			return DM_MAPIO_SUBMITTED;
		} else if (test_bit(ERROR_WRITES, &fc->flags)) {
			bio_io_error(bio);
			return DM_MAPIO_SUBMITTED;
		}

		if (!pb->bio_can_corrupt)
			goto map_bio;
		/*
		 * Corrupt matching writes.
		 */
		corrupt_fixed = false;
		corrupt_random = false;
		if (fc->corrupt_bio_byte && fc->corrupt_bio_rw == WRITE) {
			if (all_corrupt_bio_flags_match(bio, fc))
				corrupt_fixed = true;
		}
		if (fc->random_write_corrupt) {
			u64 rnd = get_random_u64();
			u32 rem = do_div(rnd, PROBABILITY_BASE);
			if (rem < fc->random_write_corrupt)
				corrupt_random = true;
		}
		if (corrupt_fixed || corrupt_random) {
			struct bio *clone = clone_bio(ti, fc, bio);
			if (clone) {
				if (corrupt_fixed)
					corrupt_bio_data(clone, fc,
							 clone->bi_iter);
				if (corrupt_random)
					corrupt_bio_random(clone,
							   clone->bi_iter);
				submit_bio(clone);
				return DM_MAPIO_SUBMITTED;
			}
		}
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

	if (!*error && pb->bio_can_corrupt && (bio_data_dir(bio) == READ)) {
		if (fc->corrupt_bio_byte) {
			if ((fc->corrupt_bio_rw == READ) &&
			    all_corrupt_bio_flags_match(bio, fc)) {
				/*
				 * Corrupt successful matching READs while in down state.
				 */
				corrupt_bio_data(bio, fc, pb->saved_iter);
			}
		}
		if (fc->random_read_corrupt) {
			u64 rnd = get_random_u64();
			u32 rem = do_div(rnd, PROBABILITY_BASE);
			if (rem < fc->random_read_corrupt)
				corrupt_bio_random(bio, pb->saved_iter);
		}
	}

	return DM_ENDIO_DONE;
}

static void flakey_status(struct dm_target *ti, status_type_t type,
			  unsigned int status_flags, char *result, unsigned int maxlen)
{
	unsigned int sz = 0;
	struct flakey_c *fc = ti->private;
	unsigned int error_reads, drop_writes, error_writes;

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		DMEMIT("%s %llu %u %u", fc->dev->name,
		       (unsigned long long)fc->start, fc->up_interval,
		       fc->down_interval);

		error_reads = test_bit(ERROR_READS, &fc->flags);
		drop_writes = test_bit(DROP_WRITES, &fc->flags);
		error_writes = test_bit(ERROR_WRITES, &fc->flags);
		DMEMIT(" %u", error_reads + drop_writes + error_writes +
			(fc->corrupt_bio_byte > 0) * 5 +
			(fc->random_read_corrupt > 0) * 2 +
			(fc->random_write_corrupt > 0) * 2);

		if (error_reads)
			DMEMIT(" error_reads");
		if (drop_writes)
			DMEMIT(" drop_writes");
		else if (error_writes)
			DMEMIT(" error_writes");

		if (fc->corrupt_bio_byte)
			DMEMIT(" corrupt_bio_byte %u %c %u %u",
			       fc->corrupt_bio_byte,
			       (fc->corrupt_bio_rw == WRITE) ? 'w' : 'r',
			       fc->corrupt_bio_value, fc->corrupt_bio_flags);

		if (fc->random_read_corrupt > 0)
			DMEMIT(" random_read_corrupt %u", fc->random_read_corrupt);
		if (fc->random_write_corrupt > 0)
			DMEMIT(" random_write_corrupt %u", fc->random_write_corrupt);

		break;

	case STATUSTYPE_IMA:
		result[0] = '\0';
		break;
	}
}

static int flakey_prepare_ioctl(struct dm_target *ti, struct block_device **bdev,
				unsigned int cmd, unsigned long arg,
				bool *forward)
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
module_dm(flakey);

MODULE_DESCRIPTION(DM_NAME " flakey target");
MODULE_AUTHOR("Joe Thornber <dm-devel@lists.linux.dev>");
MODULE_LICENSE("GPL");
