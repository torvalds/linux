/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2015 Google, Inc.
 *
 * Author: Mikulas Patocka <mpatocka@redhat.com>
 *
 * Based on Chromium dm-verity driver (C) 2011 The Chromium OS Authors
 */

#ifndef DM_VERITY_H
#define DM_VERITY_H

#include <linux/dm-bufio.h>
#include <linux/device-mapper.h>
#include <linux/interrupt.h>
#include <crypto/hash.h>

#define DM_VERITY_MAX_LEVELS		63

enum verity_mode {
	DM_VERITY_MODE_EIO,
	DM_VERITY_MODE_LOGGING,
	DM_VERITY_MODE_RESTART,
	DM_VERITY_MODE_PANIC
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
	struct crypto_ahash *tfm;
	u8 *root_digest;	/* digest of the root block */
	u8 *salt;		/* salt: its size is salt_size */
	u8 *zero_digest;	/* digest for a zero block */
	unsigned int salt_size;
	sector_t data_start;	/* data offset in 512-byte sectors */
	sector_t hash_start;	/* hash start in blocks */
	sector_t data_blocks;	/* the number of data blocks */
	sector_t hash_blocks;	/* the number of hash blocks */
	unsigned char data_dev_block_bits;	/* log2(data blocksize) */
	unsigned char hash_dev_block_bits;	/* log2(hash blocksize) */
	unsigned char hash_per_block_bits;	/* log2(hashes in hash block) */
	unsigned char levels;	/* the number of tree levels */
	unsigned char version;
	bool hash_failed:1;	/* set if hash of any block failed */
	bool use_tasklet:1;	/* try to verify in tasklet before work-queue */
	unsigned int digest_size;	/* digest size for the current hash algorithm */
	unsigned int ahash_reqsize;/* the size of temporary space for crypto */
	enum verity_mode mode;	/* mode for handling verification errors */
	unsigned int corrupted_errs;/* Number of errors for corrupted blocks */

	struct workqueue_struct *verify_wq;

	/* starting blocks for each tree level. 0 is the lowest level. */
	sector_t hash_level_block[DM_VERITY_MAX_LEVELS];

	struct dm_verity_fec *fec;	/* forward error correction */
	unsigned long *validated_blocks; /* bitset blocks validated */

	char *signature_key_desc; /* signature keyring reference */
};

struct dm_verity_io {
	struct dm_verity *v;

	/* original value of bio->bi_end_io */
	bio_end_io_t *orig_bi_end_io;

	sector_t block;
	unsigned int n_blocks;
	bool in_tasklet;

	struct bvec_iter iter;

	struct work_struct work;
	struct tasklet_struct tasklet;

	/*
	 * Three variably-size fields follow this struct:
	 *
	 * u8 hash_req[v->ahash_reqsize];
	 * u8 real_digest[v->digest_size];
	 * u8 want_digest[v->digest_size];
	 *
	 * To access them use: verity_io_hash_req(), verity_io_real_digest()
	 * and verity_io_want_digest().
	 */
};

static inline struct ahash_request *verity_io_hash_req(struct dm_verity *v,
						     struct dm_verity_io *io)
{
	return (struct ahash_request *)(io + 1);
}

static inline u8 *verity_io_real_digest(struct dm_verity *v,
					struct dm_verity_io *io)
{
	return (u8 *)(io + 1) + v->ahash_reqsize;
}

static inline u8 *verity_io_want_digest(struct dm_verity *v,
					struct dm_verity_io *io)
{
	return (u8 *)(io + 1) + v->ahash_reqsize + v->digest_size;
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

extern int verity_hash(struct dm_verity *v, struct ahash_request *req,
		       const u8 *data, size_t len, u8 *digest);

extern int verity_hash_for_block(struct dm_verity *v, struct dm_verity_io *io,
				 sector_t block, u8 *digest, bool *is_zero);

extern bool dm_is_verity_target(struct dm_target *ti);
extern int dm_verity_get_mode(struct dm_target *ti);
extern int dm_verity_get_root_digest(struct dm_target *ti, u8 **root_digest,
				     unsigned int *digest_size);

#endif /* DM_VERITY_H */
