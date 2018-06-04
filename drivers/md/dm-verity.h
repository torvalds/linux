/*
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2015 Google, Inc.
 *
 * Author: Mikulas Patocka <mpatocka@redhat.com>
 *
 * Based on Chromium dm-verity driver (C) 2011 The Chromium OS Authors
 *
 * This file is released under the GPLv2.
 */

#ifndef DM_VERITY_H
#define DM_VERITY_H

#include "dm-bufio.h"
#include <linux/device-mapper.h>
#include <crypto/hash.h>

#define DM_VERITY_MAX_LEVELS		63

enum verity_mode {
	DM_VERITY_MODE_EIO,
	DM_VERITY_MODE_LOGGING,
	DM_VERITY_MODE_RESTART
};

enum verity_block_type {
	DM_VERITY_BLOCK_TYPE_DATA,
	DM_VERITY_BLOCK_TYPE_METADATA
};

struct dm_verity_fec;

struct dm_verity {
	struct dm_dev *data_dev;
	struct dm_dev *hash_dev;
	struct dm_target *ti;
	struct dm_bufio_client *bufio;
	char *alg_name;
	struct crypto_shash *tfm;
	u8 *root_digest;	/* digest of the root block */
	u8 *salt;		/* salt: its size is salt_size */
	u8 *zero_digest;	/* digest for a zero block */
	unsigned salt_size;
	sector_t data_start;	/* data offset in 512-byte sectors */
	sector_t hash_start;	/* hash start in blocks */
	sector_t data_blocks;	/* the number of data blocks */
	sector_t hash_blocks;	/* the number of hash blocks */
	unsigned char data_dev_block_bits;	/* log2(data blocksize) */
	unsigned char hash_dev_block_bits;	/* log2(hash blocksize) */
	unsigned char hash_per_block_bits;	/* log2(hashes in hash block) */
	unsigned char levels;	/* the number of tree levels */
	unsigned char version;
	unsigned digest_size;	/* digest size for the current hash algorithm */
	unsigned shash_descsize;/* the size of temporary space for crypto */
	int hash_failed;	/* set to 1 if hash of any block failed */
	enum verity_mode mode;	/* mode for handling verification errors */
	unsigned corrupted_errs;/* Number of errors for corrupted blocks */

	struct workqueue_struct *verify_wq;

	/* starting blocks for each tree level. 0 is the lowest level. */
	sector_t hash_level_block[DM_VERITY_MAX_LEVELS];

	struct dm_verity_fec *fec;	/* forward error correction */
	unsigned long *validated_blocks; /* bitset blocks validated */
};

struct dm_verity_io {
	struct dm_verity *v;

	/* original value of bio->bi_end_io */
	bio_end_io_t *orig_bi_end_io;

	sector_t block;
	unsigned n_blocks;

	struct bvec_iter iter;

	struct work_struct work;

	/*
	 * Three variably-size fields follow this struct:
	 *
	 * u8 hash_desc[v->shash_descsize];
	 * u8 real_digest[v->digest_size];
	 * u8 want_digest[v->digest_size];
	 *
	 * To access them use: verity_io_hash_desc(), verity_io_real_digest()
	 * and verity_io_want_digest().
	 */
};

static inline struct shash_desc *verity_io_hash_desc(struct dm_verity *v,
						     struct dm_verity_io *io)
{
	return (struct shash_desc *)(io + 1);
}

static inline u8 *verity_io_real_digest(struct dm_verity *v,
					struct dm_verity_io *io)
{
	return (u8 *)(io + 1) + v->shash_descsize;
}

static inline u8 *verity_io_want_digest(struct dm_verity *v,
					struct dm_verity_io *io)
{
	return (u8 *)(io + 1) + v->shash_descsize + v->digest_size;
}

static inline u8 *verity_io_digest_end(struct dm_verity *v,
				       struct dm_verity_io *io)
{
	return verity_io_want_digest(v, io) + v->digest_size;
}

extern int verity_for_bv_block(struct dm_verity *v, struct dm_verity_io *io,
			       struct bvec_iter *iter,
			       int (*process)(struct dm_verity *v,
					      struct dm_verity_io *io,
					      u8 *data, size_t len));

extern int verity_hash(struct dm_verity *v, struct shash_desc *desc,
		       const u8 *data, size_t len, u8 *digest);

extern int verity_hash_for_block(struct dm_verity *v, struct dm_verity_io *io,
				 sector_t block, u8 *digest, bool *is_zero);

extern void verity_status(struct dm_target *ti, status_type_t type,
			unsigned status_flags, char *result, unsigned maxlen);
extern int verity_prepare_ioctl(struct dm_target *ti,
                struct block_device **bdev, fmode_t *mode);
extern int verity_iterate_devices(struct dm_target *ti,
				iterate_devices_callout_fn fn, void *data);
extern void verity_io_hints(struct dm_target *ti, struct queue_limits *limits);
extern void verity_dtr(struct dm_target *ti);
extern int verity_ctr(struct dm_target *ti, unsigned argc, char **argv);
extern int verity_map(struct dm_target *ti, struct bio *bio);
extern void dm_verity_avb_error_handler(void);
#endif /* DM_VERITY_H */
