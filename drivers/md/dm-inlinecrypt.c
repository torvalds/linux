// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */

#include <linux/blk-crypto.h>
#include <linux/ctype.h>
#include <linux/device-mapper.h>
#include <linux/hex.h>
#include <linux/module.h>
#include <keys/user-type.h>

#define DM_MSG_PREFIX	"inlinecrypt"

static const struct dm_inlinecrypt_cipher {
	const char *name;
	enum blk_crypto_mode_num mode_num;
} dm_inlinecrypt_ciphers[] = {
	{
		.name = "aes-xts-plain64",
		.mode_num = BLK_ENCRYPTION_MODE_AES_256_XTS,
	},
};

/**
 * struct inlinecrypt_ctx - private data of an inlinecrypt target
 * @dev: the underlying device
 * @start: starting sector of the range of @dev which this target actually maps.
 *	   For this purpose a "sector" is 512 bytes.
 * @cipher_string: the name of the encryption algorithm being used
 * @key_size: size of the encryption key in bytes
 * @iv_offset: starting offset for IVs.  IVs are generated as if the target were
 *	       preceded by @iv_offset 512-byte sectors.
 * @sector_size: crypto sector size in bytes (usually 4096)
 * @sector_bits: log2(sector_size)
 * @key_type: type of the key -- either raw or hardware-wrapped
 * @key: the encryption key to use
 * @max_dun: the maximum DUN that may be used (computed from other params)
 */
struct inlinecrypt_ctx {
	struct dm_dev *dev;
	sector_t start;
	const char *cipher_string;
	unsigned int key_size;
	u64 iv_offset;
	unsigned int sector_size;
	unsigned int sector_bits;
	enum blk_crypto_key_type key_type;
	struct blk_crypto_key key;
	u64 max_dun;
};

static const struct dm_inlinecrypt_cipher *
lookup_cipher(const char *cipher_string)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dm_inlinecrypt_ciphers); i++) {
		if (strcmp(cipher_string, dm_inlinecrypt_ciphers[i].name) == 0)
			return &dm_inlinecrypt_ciphers[i];
	}
	return NULL;
}

static void inlinecrypt_dtr(struct dm_target *ti)
{
	struct inlinecrypt_ctx *ctx = ti->private;

	if (ctx->dev) {
		if (ctx->key.size)
			blk_crypto_evict_key(ctx->dev->bdev, &ctx->key);
		dm_put_device(ti, ctx->dev);
	}
	kfree_sensitive(ctx->cipher_string);
	kfree_sensitive(ctx);
}

#ifdef CONFIG_KEYS

static bool contains_whitespace(const char *str)
{
	while (*str)
		if (isspace(*str++))
			return true;
	return false;
}

static int set_key_user(struct key *key, char *key_bytes,
			const unsigned int key_bytes_size)
{
	const struct user_key_payload *ukp;

	ukp = user_key_payload_locked(key);
	if (!ukp)
		return -EKEYREVOKED;

	if (key_bytes_size != ukp->datalen)
		return -EINVAL;

	memcpy(key_bytes, ukp->data, key_bytes_size);

	return 0;
}

static int inlinecrypt_get_keyring_key(const char *key_string, u8 *key_bytes,
					const unsigned int key_bytes_size)
{
	char *key_desc;
	int ret;
	struct key_type *type;
	struct key *key;
	int (*set_key)(struct key *key, char *key_bytes,
				   const unsigned int key_bytes_size);

	/*
	 * Reject key_string with whitespace. dm core currently lacks code for
	 * proper whitespace escaping in arguments on DM_TABLE_STATUS path.
	 */
	if (contains_whitespace(key_string)) {
		DMERR("whitespace chars not allowed in key string");
		return -EINVAL;
	}

	/* look for next ':' separating key_type from key_description */
	key_desc = strchr(key_string, ':');
	if (!key_desc || key_desc == key_string || !strlen(key_desc + 1))
		return -EINVAL;

	if (!strncmp(key_string, "logon:", key_desc - key_string + 1)) {
		type = &key_type_logon;
		set_key = set_key_user;
	} else {
		return -EINVAL;
	}

	key = request_key(type, key_desc + 1, NULL);
	if (IS_ERR(key))
		return PTR_ERR(key);

	down_read(&key->sem);

	ret = set_key(key, (char *)key_bytes, key_bytes_size);

	up_read(&key->sem);
	key_put(key);

	return ret;
}

static int get_key_size(char **key_string)
{
	char *colon, dummy;
	int ret;

	if (*key_string[0] != ':') {
		ret = strlen(*key_string);

		if (ret > 2 * BLK_CRYPTO_MAX_ANY_KEY_SIZE
			|| ret  % 2
			|| !ret) {
			DMERR("Invalid keysize");
			return -EINVAL;
		}
		return ret >> 1;
	}

	/* look for next ':' in key string */
	colon = strpbrk(*key_string + 1, ":");
	if (!colon)
		return -EINVAL;

	if (sscanf(*key_string + 1, "%u%c", &ret, &dummy) != 2 || dummy != ':')
		return -EINVAL;

	/* remaining key string should be :<logon|user>:<key_desc> */
	*key_string = colon;

	return ret;
}

#else

static int inlinecrypt_get_keyring_key(const char *key_string, u8 *key_bytes,
					const unsigned int key_bytes_size)
{
	return -EINVAL;
}

static int get_key_size(char **key_string)
{
	int key_hex_size = strlen(*key_string);

	if (*key_string[0] == ':')
		return -EINVAL;

	if (key_hex_size > 2 * BLK_CRYPTO_MAX_ANY_KEY_SIZE
		|| key_hex_size  % 2
		|| !key_hex_size) {
		DMERR("Invalid keysize");
		return -EINVAL;
	}

	return key_hex_size >> 1;
}

#endif /* CONFIG_KEYS */

static int inlinecrypt_get_key(const char *key_string,
				u8 key[BLK_CRYPTO_MAX_ANY_KEY_SIZE],
				const unsigned int key_size)
{
	int ret = 0;

	if (key_size > BLK_CRYPTO_MAX_ANY_KEY_SIZE) {
		DMERR("Invalid keysize");
		return -EINVAL;
	}

	/* ':' means the key is in kernel keyring, short-circuit normal key processing */
	if (key_string[0] == ':') {
		/* key string should be :<logon|user>:<key_desc> */
		ret = inlinecrypt_get_keyring_key(key_string + 1, key, key_size);
		goto out;
	}

	if (hex2bin(key, key_string, key_size) != 0)
		ret = -EINVAL;

out:
	return ret;
}

static int inlinecrypt_ctr_optional(struct dm_target *ti,
				    unsigned int argc, char **argv)
{
	struct inlinecrypt_ctx *ctx = ti->private;
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
		if (str_has_prefix(opt_string, "keytype:")) {
			const char *val = opt_string + strlen("keytype:");

			if (!*val) {
				ti->error = "Invalid block key type";
				return -EINVAL;
			}

			if (!strcmp(val, "raw")) {
				ctx->key_type = BLK_CRYPTO_KEY_TYPE_RAW;
			} else if (!strcmp(val, "hw-wrapped")) {
				ctx->key_type = BLK_CRYPTO_KEY_TYPE_HW_WRAPPED;
			} else {
				ti->error = "Invalid block key type";
				return -EINVAL;
			}
		} else if (!strcmp(opt_string, "allow_discards")) {
			ti->num_discard_bios = 1;
		} else if (sscanf(opt_string, "sector_size:%u%c",
				  &ctx->sector_size, &dummy) == 1) {
			if (ctx->sector_size < SECTOR_SIZE ||
			    ctx->sector_size > 4096 ||
			    !is_power_of_2(ctx->sector_size)) {
				ti->error = "Invalid sector_size";
				return -EINVAL;
			}
		} else if (!strcmp(opt_string, "iv_large_sectors")) {
			iv_large_sectors = true;
		} else {
			ti->error = "Invalid feature arguments";
			return -EINVAL;
		}
	}

	/* dm-inlinecrypt doesn't implement iv_large_sectors=false. */
	if (ctx->sector_size != SECTOR_SIZE && !iv_large_sectors) {
		ti->error = "iv_large_sectors must be specified";
		return -EINVAL;
	}

	return 0;
}

/*
 * Construct an inlinecrypt mapping:
 * <cipher> [<key>|:<key_size>:<logon>:<key_description>] <iv_offset> <dev_path> <start>
 *
 * This syntax matches dm-crypt's, but the set of supported functionality has
 * been stripped down.
 */
static int inlinecrypt_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct inlinecrypt_ctx *ctx;
	const struct dm_inlinecrypt_cipher *cipher;
	u8 key_bytes[BLK_CRYPTO_MAX_ANY_KEY_SIZE];
	unsigned int dun_bytes;
	unsigned long long tmpll;
	char dummy;
	int err;

	if (argc < 5) {
		ti->error = "Not enough arguments";
		return -EINVAL;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ti->error = "Out of memory";
		return -ENOMEM;
	}
	ti->private = ctx;

	/* <cipher> */
	ctx->cipher_string = kstrdup(argv[0], GFP_KERNEL);
	if (!ctx->cipher_string) {
		ti->error = "Out of memory";
		err = -ENOMEM;
		goto bad;
	}
	cipher = lookup_cipher(ctx->cipher_string);
	if (!cipher) {
		ti->error = "Unsupported cipher";
		err = -EINVAL;
		goto bad;
	}

	/* <key> */
	err = get_key_size(&argv[1]);
	if (err < 0) {
		ti->error = "Cannot parse key size";
		return -EINVAL;
	}
	ctx->key_size = err;

	err = inlinecrypt_get_key(argv[1], key_bytes, ctx->key_size);
	if (err) {
		ti->error = "Malformed key string";
		goto bad;
	}

	/* <iv_offset> */
	if (sscanf(argv[2], "%llu%c", &ctx->iv_offset, &dummy) != 1) {
		ti->error = "Invalid iv_offset sector";
		err = -EINVAL;
		goto bad;
	}

	/* <dev_path> */
	err = dm_get_device(ti, argv[3], dm_table_get_mode(ti->table),
			    &ctx->dev);
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
	ctx->start = tmpll;

	/* optional arguments */
	ctx->sector_size = SECTOR_SIZE;
	ctx->key_type = BLK_CRYPTO_KEY_TYPE_RAW;
	if (argc > 5) {
		err = inlinecrypt_ctr_optional(ti, argc - 5, &argv[5]);
		if (err)
			goto bad;
	}
	ctx->sector_bits = ilog2(ctx->sector_size);
	if (ti->len & ((ctx->sector_size >> SECTOR_SHIFT) - 1)) {
		ti->error = "Device size is not a multiple of sector_size";
		err = -EINVAL;
		goto bad;
	}
	if (ctx->iv_offset & ((ctx->sector_size >> SECTOR_SHIFT) - 1)) {
		ti->error = "Wrong alignment of iv_offset sector";
		err = -EINVAL;
	}

	ctx->max_dun = (ctx->iv_offset + ti->len - 1) >>
		       (ctx->sector_bits - SECTOR_SHIFT);
	dun_bytes = DIV_ROUND_UP(fls64(ctx->max_dun), 8);

	err = blk_crypto_init_key(&ctx->key, key_bytes, ctx->key_size,
				  ctx->key_type, cipher->mode_num,
				  dun_bytes, ctx->sector_size);
	if (err) {
		ti->error = "Error initializing blk-crypto key";
		goto bad;
	}

	err = blk_crypto_start_using_key(ctx->dev->bdev, &ctx->key);
	if (err) {
		ti->error = "Error starting to use blk-crypto";
		goto bad;
	}

	ti->num_flush_bios = 1;

	err = 0;
	goto out;

bad:
	inlinecrypt_dtr(ti);
out:
	memzero_explicit(key_bytes, sizeof(key_bytes));
	return err;
}

static int inlinecrypt_map(struct dm_target *ti, struct bio *bio)
{
	const struct inlinecrypt_ctx *ctx = ti->private;
	sector_t sector_in_target;
	u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE] = {};

	bio_set_dev(bio, ctx->dev->bdev);

	/*
	 * If the bio is a device-level request which doesn't target a specific
	 * sector, there's nothing more to do.
	 */
	if (bio_sectors(bio) == 0)
		return DM_MAPIO_REMAPPED;

	/*
	 * The bio should never have an encryption context already, since
	 * dm-inlinecrypt doesn't pass through any inline encryption
	 * capabilities to the layer above it.
	 */
	if (WARN_ON_ONCE(bio_has_crypt_ctx(bio)))
		return DM_MAPIO_KILL;

	/* Map the bio's sector to the underlying device. (512-byte sectors) */
	sector_in_target = dm_target_offset(ti, bio->bi_iter.bi_sector);
	bio->bi_iter.bi_sector = ctx->start + sector_in_target;
	/*
	 * If the bio doesn't have any data (e.g. if it's a DISCARD request),
	 * there's nothing more to do.
	 */
	if (!bio_has_data(bio))
		return DM_MAPIO_REMAPPED;

	/* Calculate the DUN and enforce data-unit (crypto sector) alignment. */
	dun[0] = ctx->iv_offset + sector_in_target; /* 512-byte sectors */
	if (dun[0] & ((ctx->sector_size >> SECTOR_SHIFT) - 1))
		return DM_MAPIO_KILL;
	dun[0] >>= ctx->sector_bits - SECTOR_SHIFT; /* crypto sectors */

	/*
	 * This check isn't necessary as we should have calculated max_dun
	 * correctly, but be safe.
	 */
	if (WARN_ON_ONCE(dun[0] > ctx->max_dun))
		return DM_MAPIO_KILL;

	bio_crypt_set_ctx(bio, &ctx->key, dun, GFP_NOIO);

	/*
	 * Since we've added an encryption context to the bio and
	 * blk-crypto-fallback may be needed to process it, it's necessary to
	 * use the fallback-aware bio submission code rather than
	 * unconditionally returning DM_MAPIO_REMAPPED.
	 *
	 * To get the correct accounting for a dm target in the case where
	 * __blk_crypto_submit_bio() doesn't take ownership of the bio (returns
	 * true), call __blk_crypto_submit_bio() directly and return
	 * DM_MAPIO_REMAPPED in that case, rather than relying on
	 * blk_crypto_submit_bio() which calls submit_bio() in that case.
	 *
	 * TODO: blk-crypto fallback write slow-path currently double-accounts
	 * IO in vmstat, as encrypted bios are submitted via submit_bio().
	 * This does not affect data correctness. Consider fixing this if
	 * a cleaner accounting model for derived bios is introduced.
	 */
	if (__blk_crypto_submit_bio(bio))
		return DM_MAPIO_REMAPPED;
	return DM_MAPIO_SUBMITTED;
}

static void inlinecrypt_status(struct dm_target *ti, status_type_t type,
			       unsigned int status_flags, char *result,
			       unsigned int maxlen)
{
	const struct inlinecrypt_ctx *ctx = ti->private;
	unsigned int sz = 0;
	int num_feature_args = 0;

	switch (type) {
	case STATUSTYPE_INFO:
	case STATUSTYPE_IMA:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		/*
		 * Warning: like dm-crypt, dm-inlinecrypt includes the key in
		 * the returned table.  Userspace is responsible for redacting
		 * the key when needed.
		 */
		DMEMIT("%s %*phN %u %llu %s %llu", ctx->cipher_string,
		       ctx->key.size, ctx->key.bytes,
		       ctx->key_type, ctx->iv_offset,
		       ctx->dev->name, ctx->start);
		num_feature_args += !!ti->num_discard_bios;
		if (ctx->sector_size != SECTOR_SIZE)
			num_feature_args += 2;
		if (num_feature_args != 0) {
			DMEMIT(" %d", num_feature_args);
			if (ti->num_discard_bios)
				DMEMIT(" allow_discards");
			if (ctx->sector_size != SECTOR_SIZE) {
				DMEMIT(" sector_size:%u", ctx->sector_size);
				DMEMIT(" iv_large_sectors");
			}
		}
		break;
	}
}

static int inlinecrypt_prepare_ioctl(struct dm_target *ti,
				     struct block_device **bdev, unsigned int cmd,
				     unsigned long arg, bool *forward)
{
	const struct inlinecrypt_ctx *ctx = ti->private;
	const struct dm_dev *dev = ctx->dev;

	*bdev = dev->bdev;

	/* Only pass ioctls through if the device sizes match exactly. */
	return ctx->start != 0 || ti->len != bdev_nr_sectors(dev->bdev);
}

static int inlinecrypt_iterate_devices(struct dm_target *ti,
				       iterate_devices_callout_fn fn,
				       void *data)
{
	const struct inlinecrypt_ctx *ctx = ti->private;

	return fn(ti, ctx->dev, ctx->start, ti->len, data);
}

#ifdef CONFIG_BLK_DEV_ZONED
static int inlinecrypt_report_zones(struct dm_target *ti,
				    struct dm_report_zones_args *args,
				    unsigned int nr_zones)
{
	const struct inlinecrypt_ctx *ctx = ti->private;

	return dm_report_zones(ctx->dev->bdev, ctx->start,
			ctx->start + dm_target_offset(ti, args->next_sector),
			args, nr_zones);
}
#else
#define inlinecrypt_report_zones NULL
#endif

static void inlinecrypt_io_hints(struct dm_target *ti,
				 struct queue_limits *limits)
{
	const struct inlinecrypt_ctx *ctx = ti->private;
	const unsigned int sector_size = ctx->sector_size;

	limits->logical_block_size =
		max_t(unsigned int, limits->logical_block_size, sector_size);
	limits->physical_block_size =
		max_t(unsigned int, limits->physical_block_size, sector_size);
	limits->io_min = max_t(unsigned int, limits->io_min, sector_size);
	limits->dma_alignment = limits->logical_block_size - 1;
}

static struct target_type inlinecrypt_target = {
	.name			= "inlinecrypt",
	.version		= {1, 0, 0},
	/*
	 * Do not set DM_TARGET_PASSES_CRYPTO, since dm-inlinecrypt consumes the
	 * crypto capability itself.
	 */
	.features		= DM_TARGET_ZONED_HM,
	.module			= THIS_MODULE,
	.ctr			= inlinecrypt_ctr,
	.dtr			= inlinecrypt_dtr,
	.map			= inlinecrypt_map,
	.status			= inlinecrypt_status,
	.prepare_ioctl		= inlinecrypt_prepare_ioctl,
	.iterate_devices	= inlinecrypt_iterate_devices,
	.report_zones		= inlinecrypt_report_zones,
	.io_hints		= inlinecrypt_io_hints,
};

module_dm(inlinecrypt);

MODULE_AUTHOR("Eric Biggers <ebiggers@google.com>");
MODULE_AUTHOR("Linlin Zhang <linlin.zhang@oss.qualcomm.com>");
MODULE_DESCRIPTION(DM_NAME " target for inline encryption");
MODULE_LICENSE("GPL");
