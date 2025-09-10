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

#include <linux/dm-io.h>
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
	struct crypto_shash *shash_tfm;
	u8 *root_digest;	/* digest of the root block */
	u8 *salt;		/* salt: its size is salt_size */
	u8 *initial_hashstate;	/* salted initial state, if version >= 1 */
	u8 *zero_digest;	/* digest for a zero block */
#ifdef CONFIG_SECURITY
	u8 *root_digest_sig;	/* signature of the root digest */
	unsigned int sig_size;	/* root digest signature size */
#endif /* CONFIG_SECURITY */
	unsigned int salt_size;
	sector_t hash_start;	/* hash start in blocks */
	sector_t data_blocks;	/* the number of data blocks */
	sector_t hash_blocks;	/* the number of hash blocks */
	unsigned char data_dev_block_bits;	/* log2(data blocksize) */
	unsigned char hash_dev_block_bits;	/* log2(hash blocksize) */
	unsigned char hash_per_block_bits;	/* log2(hashes in hash block) */
	unsigned char levels;	/* the number of tree levels */
	unsigned char version;
	bool hash_failed:1;	/* set if hash of any block failed */
	bool use_bh_wq:1;	/* try to verify in BH wq before normal work-queue */
	unsigned int digest_size;	/* digest size for the current hash algorithm */
	enum verity_mode mode;	/* mode for handling verification errors */
	enum verity_mode error_mode;/* mode for handling I/O errors */
	unsigned int corrupted_errs;/* Number of errors for corrupted blocks */

	struct workqueue_struct *verify_wq;

	/* starting blocks for each tree level. 0 is the lowest level. */
	sector_t hash_level_block[DM_VERITY_MAX_LEVELS];

	struct dm_verity_fec *fec;	/* forward error correction */
	unsigned long *validated_blocks; /* bitset blocks validated */

	char *signature_key_desc; /* signature keyring reference */

	struct dm_io_client *io;
	mempool_t recheck_pool;
};

struct dm_verity_io {
	struct dm_verity *v;

	/* original value of bio->bi_end_io */
	bio_end_io_t *orig_bi_end_io;

	struct bvec_iter iter;

	sector_t block;
	unsigned int n_blocks;
	bool in_bh;
	bool had_mismatch;

	struct work_struct work;
	struct work_struct bh_work;

	u8 real_digest[HASH_MAX_DIGESTSIZE];
	u8 want_digest[HASH_MAX_DIGESTSIZE];

	/*
	 * Temporary space for hashing.  This is variable-length and must be at
	 * the end of the struct.  struct shash_desc is just the fixed part;
	 * it's followed by a context of size crypto_shash_descsize(shash_tfm).
	 */
	struct shash_desc hash_desc;
};

static inline u8 *verity_io_real_digest(struct dm_verity *v,
					struct dm_verity_io *io)
{
	return io->real_digest;
}

static inline u8 *verity_io_want_digest(struct dm_verity *v,
					struct dm_verity_io *io)
{
	return io->want_digest;
}

extern int verity_hash(struct dm_verity *v, struct dm_verity_io *io,
		       const u8 *data, size_t len, u8 *digest);

extern int verity_hash_for_block(struct dm_verity *v, struct dm_verity_io *io,
				 sector_t block, u8 *digest, bool *is_zero);

extern bool dm_is_verity_target(struct dm_target *ti);
extern int dm_verity_get_mode(struct dm_target *ti);
extern int dm_verity_get_root_digest(struct dm_target *ti, u8 **root_digest,
				     unsigned int *digest_size);

#endif /* DM_VERITY_H */
