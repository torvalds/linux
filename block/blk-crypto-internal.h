/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */

#ifndef __LINUX_BLK_CRYPTO_INTERNAL_H
#define __LINUX_BLK_CRYPTO_INTERNAL_H

#include <linux/bio.h>
#include <linux/blk-mq.h>

/* Represents a crypto mode supported by blk-crypto  */
struct blk_crypto_mode {
	const char *name; /* name of this mode, shown in sysfs */
	const char *cipher_str; /* crypto API name (for fallback case) */
	unsigned int keysize; /* key size in bytes */
	unsigned int ivsize; /* iv size in bytes */
};

extern const struct blk_crypto_mode blk_crypto_modes[];

#ifdef CONFIG_BLK_INLINE_ENCRYPTION

int blk_crypto_sysfs_register(struct request_queue *q);

void blk_crypto_sysfs_unregister(struct request_queue *q);

void bio_crypt_dun_increment(u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE],
			     unsigned int inc);

bool bio_crypt_rq_ctx_compatible(struct request *rq, struct bio *bio);

bool bio_crypt_ctx_mergeable(struct bio_crypt_ctx *bc1, unsigned int bc1_bytes,
			     struct bio_crypt_ctx *bc2);

static inline bool bio_crypt_ctx_back_mergeable(struct request *req,
						struct bio *bio)
{
	return bio_crypt_ctx_mergeable(req->crypt_ctx, blk_rq_bytes(req),
				       bio->bi_crypt_context);
}

static inline bool bio_crypt_ctx_front_mergeable(struct request *req,
						 struct bio *bio)
{
	return bio_crypt_ctx_mergeable(bio->bi_crypt_context,
				       bio->bi_iter.bi_size, req->crypt_ctx);
}

static inline bool bio_crypt_ctx_merge_rq(struct request *req,
					  struct request *next)
{
	return bio_crypt_ctx_mergeable(req->crypt_ctx, blk_rq_bytes(req),
				       next->crypt_ctx);
}

static inline void blk_crypto_rq_set_defaults(struct request *rq)
{
	rq->crypt_ctx = NULL;
	rq->crypt_keyslot = NULL;
}

static inline bool blk_crypto_rq_is_encrypted(struct request *rq)
{
	return rq->crypt_ctx;
}

#else /* CONFIG_BLK_INLINE_ENCRYPTION */

static inline int blk_crypto_sysfs_register(struct request_queue *q)
{
	return 0;
}

static inline void blk_crypto_sysfs_unregister(struct request_queue *q) { }

static inline bool bio_crypt_rq_ctx_compatible(struct request *rq,
					       struct bio *bio)
{
	return true;
}

static inline bool bio_crypt_ctx_front_mergeable(struct request *req,
						 struct bio *bio)
{
	return true;
}

static inline bool bio_crypt_ctx_back_mergeable(struct request *req,
						struct bio *bio)
{
	return true;
}

static inline bool bio_crypt_ctx_merge_rq(struct request *req,
					  struct request *next)
{
	return true;
}

static inline void blk_crypto_rq_set_defaults(struct request *rq) { }

static inline bool blk_crypto_rq_is_encrypted(struct request *rq)
{
	return false;
}

#endif /* CONFIG_BLK_INLINE_ENCRYPTION */

void __bio_crypt_advance(struct bio *bio, unsigned int bytes);
static inline void bio_crypt_advance(struct bio *bio, unsigned int bytes)
{
	if (bio_has_crypt_ctx(bio))
		__bio_crypt_advance(bio, bytes);
}

void __bio_crypt_free_ctx(struct bio *bio);
static inline void bio_crypt_free_ctx(struct bio *bio)
{
	if (bio_has_crypt_ctx(bio))
		__bio_crypt_free_ctx(bio);
}

static inline void bio_crypt_do_front_merge(struct request *rq,
					    struct bio *bio)
{
#ifdef CONFIG_BLK_INLINE_ENCRYPTION
	if (bio_has_crypt_ctx(bio))
		memcpy(rq->crypt_ctx->bc_dun, bio->bi_crypt_context->bc_dun,
		       sizeof(rq->crypt_ctx->bc_dun));
#endif
}

bool __blk_crypto_bio_prep(struct bio **bio_ptr);
static inline bool blk_crypto_bio_prep(struct bio **bio_ptr)
{
	if (bio_has_crypt_ctx(*bio_ptr))
		return __blk_crypto_bio_prep(bio_ptr);
	return true;
}

blk_status_t __blk_crypto_init_request(struct request *rq);
static inline blk_status_t blk_crypto_init_request(struct request *rq)
{
	if (blk_crypto_rq_is_encrypted(rq))
		return __blk_crypto_init_request(rq);
	return BLK_STS_OK;
}

void __blk_crypto_free_request(struct request *rq);
static inline void blk_crypto_free_request(struct request *rq)
{
	if (blk_crypto_rq_is_encrypted(rq))
		__blk_crypto_free_request(rq);
}

int __blk_crypto_rq_bio_prep(struct request *rq, struct bio *bio,
			     gfp_t gfp_mask);
/**
 * blk_crypto_rq_bio_prep - Prepare a request's crypt_ctx when its first bio
 *			    is inserted
 * @rq: The request to prepare
 * @bio: The first bio being inserted into the request
 * @gfp_mask: Memory allocation flags
 *
 * Return: 0 on success, -ENOMEM if out of memory.  -ENOMEM is only possible if
 *	   @gfp_mask doesn't include %__GFP_DIRECT_RECLAIM.
 */
static inline int blk_crypto_rq_bio_prep(struct request *rq, struct bio *bio,
					 gfp_t gfp_mask)
{
	if (bio_has_crypt_ctx(bio))
		return __blk_crypto_rq_bio_prep(rq, bio, gfp_mask);
	return 0;
}

/**
 * blk_crypto_insert_cloned_request - Prepare a cloned request to be inserted
 *				      into a request queue.
 * @rq: the request being queued
 *
 * Return: BLK_STS_OK on success, nonzero on error.
 */
static inline blk_status_t blk_crypto_insert_cloned_request(struct request *rq)
{

	if (blk_crypto_rq_is_encrypted(rq))
		return blk_crypto_init_request(rq);
	return BLK_STS_OK;
}

#ifdef CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK

int blk_crypto_fallback_start_using_mode(enum blk_crypto_mode_num mode_num);

bool blk_crypto_fallback_bio_prep(struct bio **bio_ptr);

int blk_crypto_fallback_evict_key(const struct blk_crypto_key *key);

#else /* CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK */

static inline int
blk_crypto_fallback_start_using_mode(enum blk_crypto_mode_num mode_num)
{
	pr_warn_once("crypto API fallback is disabled\n");
	return -ENOPKG;
}

static inline bool blk_crypto_fallback_bio_prep(struct bio **bio_ptr)
{
	pr_warn_once("crypto API fallback disabled; failing request.\n");
	(*bio_ptr)->bi_status = BLK_STS_NOTSUPP;
	return false;
}

static inline int
blk_crypto_fallback_evict_key(const struct blk_crypto_key *key)
{
	return 0;
}

#endif /* CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK */

#endif /* __LINUX_BLK_CRYPTO_INTERNAL_H */
