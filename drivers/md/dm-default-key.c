// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Google, Inc.
 */

#include <linux/blk-crypto.h>
#include <linux/device-mapper.h>
#include <linux/module.h>

#define DM_MSG_PREFIX		"default-key"

#define DM_DEFAULT_KEY_MAX_WRAPPED_KEY_SIZE 128

static const struct dm_default_key_cipher {
	const char *name;
	enum blk_crypto_mode_num mode_num;
	int key_size;
} dm_default_key_ciphers[] = {
	{
		.name = "aes-xts-plain64",
		.mode_num = BLK_ENCRYPTION_MODE_AES_256_XTS,
		.key_size = 64,
	}, {
		.name = "xchacha12,aes-adiantum-plain64",
		.mode_num = BLK_ENCRYPTION_MODE_ADIANTUM,
		.key_size = 32,
	},
};

/**
 * struct dm_default_c - private data of a default-key target
 * @dev: the underlying device
 * @start: starting sector of the range of @dev which this target actually maps.
 *	   For this purpose a "sector" is 512 bytes.
 * @cipher_string: the name of the encryption algorithm being used
 * @iv_offset: starting offset for IVs.  IVs are generated as if the target were
 *	       preceded by @iv_offset 512-byte sectors.
 * @sector_size: crypto sector size in bytes (usually 4096)
 * @sector_bits: log2(sector_size)
 * @key: the encryption key to use
 * @max_dun: the maximum DUN that may be used (computed from other params)
 */
struct default_key_c {
	struct dm_dev *dev;
	sector_t start;
	const char *cipher_string;
	u64 iv_offset;
	unsigned int sector_size;
	unsigned int sector_bits;
	struct blk_crypto_key key;
	bool is_hw_wrapped;
	u64 max_dun;
};

static const struct dm_default_key_cipher *
lookup_cipher(const char *cipher_string)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dm_default_key_ciphers); i++) {
		if (strcmp(cipher_string, dm_default_key_ciphers[i].name) == 0)
			return &dm_default_key_ciphers[i];
	}
	return NULL;
}

static void default_key_dtr(struct dm_target *ti)
{
	struct default_key_c *dkc = ti->private;

	if (dkc->dev) {
		blk_crypto_evict_key(bdev_get_queue(dkc->dev->bdev), &dkc->key);
		dm_put_device(ti, dkc->dev);
	}
	kfree_sensitive(dkc->cipher_string);
	kfree_sensitive(dkc);
}

static int default_key_ctr_optional(struct dm_target *ti,
				    unsigned int argc, char **argv)
{
	struct default_key_c *dkc = ti->private;
	struct dm_arg_set as;
	static const struct dm_arg _args[] = {
		{0, 4, "Invalid number of feature args"},
	};
	unsigned int opt_params;
	const char *opt_string;
	bool iv_large_sectors = false;
	char dummy;
	int err;

	as.argc = argc;
	as.argv = argv;

	err = dm_read_arg_group(_args, &as, &opt_params, &ti->error);
	if (err)
		return err;

	while (opt_params--) {
		opt_string = dm_shift_arg(&as);
		if (!opt_string) {
			ti->error = "Not enough feature arguments";
			return -EINVAL;
		}
		if (!strcmp(opt_string, "allow_discards")) {
			ti->num_discard_bios = 1;
		} else if (sscanf(opt_string, "sector_size:%u%c",
				  &dkc->sector_size, &dummy) == 1) {
			if (dkc->sector_size < SECTOR_SIZE ||
			    dkc->sector_size > 4096 ||
			    !is_power_of_2(dkc->sector_size)) {
				ti->error = "Invalid sector_size";
				return -EINVAL;
			}
		} else if (!strcmp(opt_string, "iv_large_sectors")) {
			iv_large_sectors = true;
		} else if (!strcmp(opt_string, "wrappedkey_v0")) {
			dkc->is_hw_wrapped = true;
		} else {
			ti->error = "Invalid feature arguments";
			return -EINVAL;
		}
	}

	/* dm-default-key doesn't implement iv_large_sectors=false. */
	if (dkc->sector_size != SECTOR_SIZE && !iv_large_sectors) {
		ti->error = "iv_large_sectors must be specified";
		return -EINVAL;
	}

	return 0;
}

/*
 * Construct a default-key mapping:
 * <cipher> <key> <iv_offset> <dev_path> <start>
 *
 * This syntax matches dm-crypt's, but lots of unneeded functionality has been
 * removed.  Also, dm-default-key requires that the "iv_large_sectors" option be
 * given whenever a non-default sector size is used.
 */
static int default_key_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct default_key_c *dkc;
	const struct dm_default_key_cipher *cipher;
	u8 raw_key[DM_DEFAULT_KEY_MAX_WRAPPED_KEY_SIZE];
	unsigned int raw_key_size;
	unsigned int dun_bytes;
	unsigned long long tmpll;
	char dummy;
	int err;

	if (argc < 5) {
		ti->error = "Not enough arguments";
		return -EINVAL;
	}

	dkc = kzalloc(sizeof(*dkc), GFP_KERNEL);
	if (!dkc) {
		ti->error = "Out of memory";
		return -ENOMEM;
	}
	ti->private = dkc;

	/* <cipher> */
	dkc->cipher_string = kstrdup(argv[0], GFP_KERNEL);
	if (!dkc->cipher_string) {
		ti->error = "Out of memory";
		err = -ENOMEM;
		goto bad;
	}
	cipher = lookup_cipher(dkc->cipher_string);
	if (!cipher) {
		ti->error = "Unsupported cipher";
		err = -EINVAL;
		goto bad;
	}

	/* <key> */
	raw_key_size = strlen(argv[1]);
	if (raw_key_size > 2 * DM_DEFAULT_KEY_MAX_WRAPPED_KEY_SIZE ||
	    raw_key_size % 2) {
		ti->error = "Invalid keysize";
		err = -EINVAL;
		goto bad;
	}
	raw_key_size /= 2;
	if (hex2bin(raw_key, argv[1], raw_key_size) != 0) {
		ti->error = "Malformed key string";
		err = -EINVAL;
		goto bad;
	}

	/* <iv_offset> */
	if (sscanf(argv[2], "%llu%c", &dkc->iv_offset, &dummy) != 1) {
		ti->error = "Invalid iv_offset sector";
		err = -EINVAL;
		goto bad;
	}

	/* <dev_path> */
	err = dm_get_device(ti, argv[3], dm_table_get_mode(ti->table),
			    &dkc->dev);
	if (err) {
		ti->error = "Device lookup failed";
		goto bad;
	}

	/* <start> */
	if (sscanf(argv[4], "%llu%c", &tmpll, &dummy) != 1 ||
	    tmpll != (sector_t)tmpll) {
		ti->error = "Invalid start sector";
		err = -EINVAL;
		goto bad;
	}
	dkc->start = tmpll;

	/* optional arguments */
	dkc->sector_size = SECTOR_SIZE;
	if (argc > 5) {
		err = default_key_ctr_optional(ti, argc - 5, &argv[5]);
		if (err)
			goto bad;
	}
	dkc->sector_bits = ilog2(dkc->sector_size);
	if (ti->len & ((dkc->sector_size >> SECTOR_SHIFT) - 1)) {
		ti->error = "Device size is not a multiple of sector_size";
		err = -EINVAL;
		goto bad;
	}

	dkc->max_dun = (dkc->iv_offset + ti->len - 1) >>
		       (dkc->sector_bits - SECTOR_SHIFT);
	dun_bytes = DIV_ROUND_UP(fls64(dkc->max_dun), 8);

	err = blk_crypto_init_key(&dkc->key, raw_key, raw_key_size,
				  dkc->is_hw_wrapped, cipher->mode_num,
				  dun_bytes, dkc->sector_size);
	if (err) {
		ti->error = "Error initializing blk-crypto key";
		goto bad;
	}

	err = blk_crypto_start_using_key(&dkc->key,
					 bdev_get_queue(dkc->dev->bdev));
	if (err) {
		ti->error = "Error starting to use blk-crypto";
		goto bad;
	}

	ti->num_flush_bios = 1;

	err = 0;
	goto out;

bad:
	default_key_dtr(ti);
out:
	memzero_explicit(raw_key, sizeof(raw_key));
	return err;
}

static int default_key_map(struct dm_target *ti, struct bio *bio)
{
	const struct default_key_c *dkc = ti->private;
	sector_t sector_in_target;
	u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE] = { 0 };

	bio_set_dev(bio, dkc->dev->bdev);

	/*
	 * If the bio is a device-level request which doesn't target a specific
	 * sector, there's nothing more to do.
	 */
	if (bio_sectors(bio) == 0)
		return DM_MAPIO_REMAPPED;

	/* Map the bio's sector to the underlying device. (512-byte sectors) */
	sector_in_target = dm_target_offset(ti, bio->bi_iter.bi_sector);
	bio->bi_iter.bi_sector = dkc->start + sector_in_target;

	/*
	 * If the bio should skip dm-default-key (i.e. if it's for an encrypted
	 * file's contents), or if it doesn't have any data (e.g. if it's a
	 * DISCARD request), there's nothing more to do.
	 */
	if (bio_should_skip_dm_default_key(bio) || !bio_has_data(bio))
		return DM_MAPIO_REMAPPED;

	/*
	 * Else, dm-default-key needs to set this bio's encryption context.
	 * It must not already have one.
	 */
	if (WARN_ON_ONCE(bio_has_crypt_ctx(bio)))
		return DM_MAPIO_KILL;

	/* Calculate the DUN and enforce data-unit (crypto sector) alignment. */
	dun[0] = dkc->iv_offset + sector_in_target; /* 512-byte sectors */
	if (dun[0] & ((dkc->sector_size >> SECTOR_SHIFT) - 1))
		return DM_MAPIO_KILL;
	dun[0] >>= dkc->sector_bits - SECTOR_SHIFT; /* crypto sectors */

	/*
	 * This check isn't necessary as we should have calculated max_dun
	 * correctly, but be safe.
	 */
	if (WARN_ON_ONCE(dun[0] > dkc->max_dun))
		return DM_MAPIO_KILL;

	bio_crypt_set_ctx(bio, &dkc->key, dun, GFP_NOIO);

	return DM_MAPIO_REMAPPED;
}

static void default_key_status(struct dm_target *ti, status_type_t type,
			       unsigned int status_flags, char *result,
			       unsigned int maxlen)
{
	const struct default_key_c *dkc = ti->private;
	unsigned int sz = 0;
	int num_feature_args = 0;

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		/* Omit the key for now. */
		DMEMIT("%s - %llu %s %llu", dkc->cipher_string, dkc->iv_offset,
		       dkc->dev->name, (unsigned long long)dkc->start);

		num_feature_args += !!ti->num_discard_bios;
		if (dkc->sector_size != SECTOR_SIZE)
			num_feature_args += 2;
		if (dkc->is_hw_wrapped)
			num_feature_args += 1;
		if (num_feature_args != 0) {
			DMEMIT(" %d", num_feature_args);
			if (ti->num_discard_bios)
				DMEMIT(" allow_discards");
			if (dkc->sector_size != SECTOR_SIZE) {
				DMEMIT(" sector_size:%u", dkc->sector_size);
				DMEMIT(" iv_large_sectors");
			}
			if (dkc->is_hw_wrapped)
				DMEMIT(" wrappedkey_v0");
		}
		break;
	}
}

static int default_key_prepare_ioctl(struct dm_target *ti,
				     struct block_device **bdev)
{
	const struct default_key_c *dkc = ti->private;
	const struct dm_dev *dev = dkc->dev;

	*bdev = dev->bdev;

	/* Only pass ioctls through if the device sizes match exactly. */
	if (dkc->start != 0 ||
	    ti->len != i_size_read(dev->bdev->bd_inode) >> SECTOR_SHIFT)
		return 1;
	return 0;
}

static int default_key_iterate_devices(struct dm_target *ti,
				       iterate_devices_callout_fn fn,
				       void *data)
{
	const struct default_key_c *dkc = ti->private;

	return fn(ti, dkc->dev, dkc->start, ti->len, data);
}

static void default_key_io_hints(struct dm_target *ti,
				 struct queue_limits *limits)
{
	const struct default_key_c *dkc = ti->private;
	const unsigned int sector_size = dkc->sector_size;

	limits->logical_block_size =
		max_t(unsigned int, limits->logical_block_size, sector_size);
	limits->physical_block_size =
		max_t(unsigned int, limits->physical_block_size, sector_size);
	limits->io_min = max_t(unsigned int, limits->io_min, sector_size);
}

static struct target_type default_key_target = {
	.name			= "default-key",
	.version		= {2, 1, 0},
	.features		= DM_TARGET_PASSES_CRYPTO,
	.module			= THIS_MODULE,
	.ctr			= default_key_ctr,
	.dtr			= default_key_dtr,
	.map			= default_key_map,
	.status			= default_key_status,
	.prepare_ioctl		= default_key_prepare_ioctl,
	.iterate_devices	= default_key_iterate_devices,
	.io_hints		= default_key_io_hints,
};

static int __init dm_default_key_init(void)
{
	return dm_register_target(&default_key_target);
}

static void __exit dm_default_key_exit(void)
{
	dm_unregister_target(&default_key_target);
}

module_init(dm_default_key_init);
module_exit(dm_default_key_exit);

MODULE_AUTHOR("Paul Lawrence <paullawrence@google.com>");
MODULE_AUTHOR("Paul Crowley <paulcrowley@google.com>");
MODULE_AUTHOR("Eric Biggers <ebiggers@google.com>");
MODULE_DESCRIPTION(DM_NAME " target for encrypting filesystem metadata");
MODULE_LICENSE("GPL");
