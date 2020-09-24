/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */

#ifndef __LINUX_BLK_CRYPTO_INTERNAL_H
#define __LINUX_BLK_CRYPTO_INTERNAL_H

#include <linux/bio.h>

/* Represents a crypto mode supported by blk-crypto  */
struct blk_crypto_mode {
	const char *cipher_str; /* crypto API name (for fallback case) */
	unsigned int keysize; /* key size in bytes */
	unsigned int ivsize; /* iv size in bytes */
};

extern const struct blk_crypto_mode blk_crypto_modes[];

#ifdef CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK

int blk_crypto_fallback_start_using_mode(enum blk_crypto_mode_num mode_num);

int blk_crypto_fallback_submit_bio(struct bio **bio_ptr);

bool blk_crypto_queue_decrypt_bio(struct bio *bio);

int blk_crypto_fallback_evict_key(const struct blk_crypto_key *key);

bool bio_crypt_fallback_crypted(const struct bio_crypt_ctx *bc);

#else /* CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK */

static inline int
blk_crypto_fallback_start_using_mode(enum blk_crypto_mode_num mode_num)
{
	pr_warn_once("crypto API fallback is disabled\n");
	return -ENOPKG;
}

static inline bool bio_crypt_fallback_crypted(const struct bio_crypt_ctx *bc)
{
	return false;
}

static inline int blk_crypto_fallback_submit_bio(struct bio **bio_ptr)
{
	pr_warn_once("crypto API fallback disabled; failing request\n");
	(*bio_ptr)->bi_status = BLK_STS_NOTSUPP;
	return -EIO;
}

static inline bool blk_crypto_queue_decrypt_bio(struct bio *bio)
{
	WARN_ON(1);
	return false;
}

static inline int
blk_crypto_fallback_evict_key(const struct blk_crypto_key *key)
{
	return 0;
}

#endif /* CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK */

#endif /* __LINUX_BLK_CRYPTO_INTERNAL_H */
