// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

/*
 * Refer to Documentation/block/inline-encryption.rst for detailed explanation.
 */

#define pr_fmt(fmt) "blk-crypto-fallback: " fmt

#include <crypto/skcipher.h>
#include <linux/blk-crypto.h>
#include <linux/blk-crypto-profile.h>
#include <linux/blkdev.h>
#include <linux/crypto.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/scatterlist.h>

#include "blk-cgroup.h"
#include "blk-crypto-internal.h"

static unsigned int num_prealloc_bounce_pg = 32;
module_param(num_prealloc_bounce_pg, uint, 0);
MODULE_PARM_DESC(num_prealloc_bounce_pg,
		 "Number of preallocated bounce pages for the blk-crypto crypto API fallback");

static unsigned int blk_crypto_num_keyslots = 100;
module_param_named(num_keyslots, blk_crypto_num_keyslots, uint, 0);
MODULE_PARM_DESC(num_keyslots,
		 "Number of keyslots for the blk-crypto crypto API fallback");

static unsigned int num_prealloc_fallback_crypt_ctxs = 128;
module_param(num_prealloc_fallback_crypt_ctxs, uint, 0);
MODULE_PARM_DESC(num_prealloc_crypt_fallback_ctxs,
		 "Number of preallocated bio fallback crypto contexts for blk-crypto to use during crypto API fallback");

struct bio_fallback_crypt_ctx {
	struct bio_crypt_ctx crypt_ctx;
	/*
	 * Copy of the bvec_iter when this bio was submitted.
	 * We only want to en/decrypt the part of the bio as described by the
	 * bvec_iter upon submission because bio might be split before being
	 * resubmitted
	 */
	struct bvec_iter crypt_iter;
	union {
		struct {
			struct work_struct work;
			struct bio *bio;
		};
		struct {
			void *bi_private_orig;
			bio_end_io_t *bi_end_io_orig;
		};
	};
};

static struct kmem_cache *bio_fallback_crypt_ctx_cache;
static mempool_t *bio_fallback_crypt_ctx_pool;

/*
 * Allocating a crypto tfm during I/O can deadlock, so we have to preallocate
 * all of a mode's tfms when that mode starts being used. Since each mode may
 * need all the keyslots at some point, each mode needs its own tfm for each
 * keyslot; thus, a keyslot may contain tfms for multiple modes.  However, to
 * match the behavior of real inline encryption hardware (which only supports a
 * single encryption context per keyslot), we only allow one tfm per keyslot to
 * be used at a time - the rest of the unused tfms have their keys cleared.
 */
static DEFINE_MUTEX(tfms_init_lock);
static bool tfms_inited[BLK_ENCRYPTION_MODE_MAX];

static struct blk_crypto_fallback_keyslot {
	enum blk_crypto_mode_num crypto_mode;
	struct crypto_skcipher *tfms[BLK_ENCRYPTION_MODE_MAX];
} *blk_crypto_keyslots;

static struct blk_crypto_profile *blk_crypto_fallback_profile;
static struct workqueue_struct *blk_crypto_wq;
static mempool_t *blk_crypto_bounce_page_pool;
static struct bio_set crypto_bio_split;

/*
 * This is the key we set when evicting a keyslot. This *should* be the all 0's
 * key, but AES-XTS rejects that key, so we use some random bytes instead.
 */
static u8 blank_key[BLK_CRYPTO_MAX_STANDARD_KEY_SIZE];

static void blk_crypto_fallback_evict_keyslot(unsigned int slot)
{
	struct blk_crypto_fallback_keyslot *slotp = &blk_crypto_keyslots[slot];
	enum blk_crypto_mode_num crypto_mode = slotp->crypto_mode;
	int err;

	WARN_ON(slotp->crypto_mode == BLK_ENCRYPTION_MODE_INVALID);

	/* Clear the key in the skcipher */
	err = crypto_skcipher_setkey(slotp->tfms[crypto_mode], blank_key,
				     blk_crypto_modes[crypto_mode].keysize);
	WARN_ON(err);
	slotp->crypto_mode = BLK_ENCRYPTION_MODE_INVALID;
}

static int
blk_crypto_fallback_keyslot_program(struct blk_crypto_profile *profile,
				    const struct blk_crypto_key *key,
				    unsigned int slot)
{
	struct blk_crypto_fallback_keyslot *slotp = &blk_crypto_keyslots[slot];
	const enum blk_crypto_mode_num crypto_mode =
						key->crypto_cfg.crypto_mode;
	int err;

	if (crypto_mode != slotp->crypto_mode &&
	    slotp->crypto_mode != BLK_ENCRYPTION_MODE_INVALID)
		blk_crypto_fallback_evict_keyslot(slot);

	slotp->crypto_mode = crypto_mode;
	err = crypto_skcipher_setkey(slotp->tfms[crypto_mode], key->raw,
				     key->size);
	if (err) {
		blk_crypto_fallback_evict_keyslot(slot);
		return err;
	}
	return 0;
}

static int blk_crypto_fallback_keyslot_evict(struct blk_crypto_profile *profile,
					     const struct blk_crypto_key *key,
					     unsigned int slot)
{
	blk_crypto_fallback_evict_keyslot(slot);
	return 0;
}

static const struct blk_crypto_ll_ops blk_crypto_fallback_ll_ops = {
	.keyslot_program        = blk_crypto_fallback_keyslot_program,
	.keyslot_evict          = blk_crypto_fallback_keyslot_evict,
};

static void blk_crypto_fallback_encrypt_endio(struct bio *enc_bio)
{
	struct bio *src_bio = enc_bio->bi_private;
	int i;

	for (i = 0; i < enc_bio->bi_vcnt; i++)
		mempool_free(enc_bio->bi_io_vec[i].bv_page,
			     blk_crypto_bounce_page_pool);

	src_bio->bi_status = enc_bio->bi_status;

	bio_uninit(enc_bio);
	kfree(enc_bio);
	bio_endio(src_bio);
}

static struct bio *blk_crypto_fallback_clone_bio(struct bio *bio_src)
{
	unsigned int nr_segs = bio_segments(bio_src);
	struct bvec_iter iter;
	struct bio_vec bv;
	struct bio *bio;

	bio = bio_kmalloc(nr_segs, GFP_NOIO);
	if (!bio)
		return NULL;
	bio_init(bio, bio_src->bi_bdev, bio->bi_inline_vecs, nr_segs,
		 bio_src->bi_opf);
	if (bio_flagged(bio_src, BIO_REMAPPED))
		bio_set_flag(bio, BIO_REMAPPED);
	bio->bi_ioprio		= bio_src->bi_ioprio;
	bio->bi_iter.bi_sector	= bio_src->bi_iter.bi_sector;
	bio->bi_iter.bi_size	= bio_src->bi_iter.bi_size;

	bio_for_each_segment(bv, bio_src, iter)
		bio->bi_io_vec[bio->bi_vcnt++] = bv;

	bio_clone_blkg_association(bio, bio_src);

	bio_clone_skip_dm_default_key(bio, bio_src);

	return bio;
}

static bool
blk_crypto_fallback_alloc_cipher_req(struct blk_crypto_keyslot *slot,
				     struct skcipher_request **ciph_req_ret,
				     struct crypto_wait *wait)
{
	struct skcipher_request *ciph_req;
	const struct blk_crypto_fallback_keyslot *slotp;
	int keyslot_idx = blk_crypto_keyslot_index(slot);

	slotp = &blk_crypto_keyslots[keyslot_idx];
	ciph_req = skcipher_request_alloc(slotp->tfms[slotp->crypto_mode],
					  GFP_NOIO);
	if (!ciph_req)
		return false;

	skcipher_request_set_callback(ciph_req,
				      CRYPTO_TFM_REQ_MAY_BACKLOG |
				      CRYPTO_TFM_REQ_MAY_SLEEP,
				      crypto_req_done, wait);
	*ciph_req_ret = ciph_req;

	return true;
}

static bool blk_crypto_fallback_split_bio_if_needed(struct bio **bio_ptr)
{
	struct bio *bio = *bio_ptr;
	unsigned int i = 0;
	unsigned int num_sectors = 0;
	struct bio_vec bv;
	struct bvec_iter iter;

	bio_for_each_segment(bv, bio, iter) {
		num_sectors += bv.bv_len >> SECTOR_SHIFT;
		if (++i == BIO_MAX_VECS)
			break;
	}
	if (num_sectors < bio_sectors(bio)) {
		struct bio *split_bio;

		split_bio = bio_split(bio, num_sectors, GFP_NOIO,
				      &crypto_bio_split);
		if (!split_bio) {
			bio->bi_status = BLK_STS_RESOURCE;
			return false;
		}
		bio_chain(split_bio, bio);
		submit_bio_noacct(bio);
		*bio_ptr = split_bio;
	}

	return true;
}

union blk_crypto_iv {
	__le64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE];
	u8 bytes[BLK_CRYPTO_MAX_IV_SIZE];
};

static void blk_crypto_dun_to_iv(const u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE],
				 union blk_crypto_iv *iv)
{
	int i;

	for (i = 0; i < BLK_CRYPTO_DUN_ARRAY_SIZE; i++)
		iv->dun[i] = cpu_to_le64(dun[i]);
}

/*
 * The crypto API fallback's encryption routine.
 * Allocate a bounce bio for encryption, encrypt the input bio using crypto API,
 * and replace *bio_ptr with the bounce bio. May split input bio if it's too
 * large. Returns true on success. Returns false and sets bio->bi_status on
 * error.
 */
static bool blk_crypto_fallback_encrypt_bio(struct bio **bio_ptr)
{
	struct bio *src_bio, *enc_bio;
	struct bio_crypt_ctx *bc;
	struct blk_crypto_keyslot *slot;
	int data_unit_size;
	struct skcipher_request *ciph_req = NULL;
	DECLARE_CRYPTO_WAIT(wait);
	u64 curr_dun[BLK_CRYPTO_DUN_ARRAY_SIZE];
	struct scatterlist src, dst;
	union blk_crypto_iv iv;
	unsigned int i, j;
	bool ret = false;
	blk_status_t blk_st;

	/* Split the bio if it's too big for single page bvec */
	if (!blk_crypto_fallback_split_bio_if_needed(bio_ptr))
		return false;

	src_bio = *bio_ptr;
	bc = src_bio->bi_crypt_context;
	data_unit_size = bc->bc_key->crypto_cfg.data_unit_size;

	/* Allocate bounce bio for encryption */
	enc_bio = blk_crypto_fallback_clone_bio(src_bio);
	if (!enc_bio) {
		src_bio->bi_status = BLK_STS_RESOURCE;
		return false;
	}

	/*
	 * Get a blk-crypto-fallback keyslot that contains a crypto_skcipher for
	 * this bio's algorithm and key.
	 */
	blk_st = blk_crypto_get_keyslot(blk_crypto_fallback_profile,
					bc->bc_key, &slot);
	if (blk_st != BLK_STS_OK) {
		src_bio->bi_status = blk_st;
		goto out_put_enc_bio;
	}

	/* and then allocate an skcipher_request for it */
	if (!blk_crypto_fallback_alloc_cipher_req(slot, &ciph_req, &wait)) {
		src_bio->bi_status = BLK_STS_RESOURCE;
		goto out_release_keyslot;
	}

	memcpy(curr_dun, bc->bc_dun, sizeof(curr_dun));
	sg_init_table(&src, 1);
	sg_init_table(&dst, 1);

	skcipher_request_set_crypt(ciph_req, &src, &dst, data_unit_size,
				   iv.bytes);

	/* Encrypt each page in the bounce bio */
	for (i = 0; i < enc_bio->bi_vcnt; i++) {
		struct bio_vec *enc_bvec = &enc_bio->bi_io_vec[i];
		struct page *plaintext_page = enc_bvec->bv_page;
		struct page *ciphertext_page =
			mempool_alloc(blk_crypto_bounce_page_pool, GFP_NOIO);

		enc_bvec->bv_page = ciphertext_page;

		if (!ciphertext_page) {
			src_bio->bi_status = BLK_STS_RESOURCE;
			goto out_free_bounce_pages;
		}

		sg_set_page(&src, plaintext_page, data_unit_size,
			    enc_bvec->bv_offset);
		sg_set_page(&dst, ciphertext_page, data_unit_size,
			    enc_bvec->bv_offset);

		/* Encrypt each data unit in this page */
		for (j = 0; j < enc_bvec->bv_len; j += data_unit_size) {
			blk_crypto_dun_to_iv(curr_dun, &iv);
			if (crypto_wait_req(crypto_skcipher_encrypt(ciph_req),
					    &wait)) {
				i++;
				src_bio->bi_status = BLK_STS_IOERR;
				goto out_free_bounce_pages;
			}
			bio_crypt_dun_increment(curr_dun, 1);
			src.offset += data_unit_size;
			dst.offset += data_unit_size;
		}
	}

	enc_bio->bi_private = src_bio;
	enc_bio->bi_end_io = blk_crypto_fallback_encrypt_endio;
	*bio_ptr = enc_bio;
	ret = true;

	enc_bio = NULL;
	goto out_free_ciph_req;

out_free_bounce_pages:
	while (i > 0)
		mempool_free(enc_bio->bi_io_vec[--i].bv_page,
			     blk_crypto_bounce_page_pool);
out_free_ciph_req:
	skcipher_request_free(ciph_req);
out_release_keyslot:
	blk_crypto_put_keyslot(slot);
out_put_enc_bio:
	if (enc_bio)
		bio_uninit(enc_bio);
	kfree(enc_bio);
	return ret;
}

/*
 * The crypto API fallback's main decryption routine.
 * Decrypts input bio in place, and calls bio_endio on the bio.
 */
static void blk_crypto_fallback_decrypt_bio(struct work_struct *work)
{
	struct bio_fallback_crypt_ctx *f_ctx =
		container_of(work, struct bio_fallback_crypt_ctx, work);
	struct bio *bio = f_ctx->bio;
	struct bio_crypt_ctx *bc = &f_ctx->crypt_ctx;
	struct blk_crypto_keyslot *slot;
	struct skcipher_request *ciph_req = NULL;
	DECLARE_CRYPTO_WAIT(wait);
	u64 curr_dun[BLK_CRYPTO_DUN_ARRAY_SIZE];
	union blk_crypto_iv iv;
	struct scatterlist sg;
	struct bio_vec bv;
	struct bvec_iter iter;
	const int data_unit_size = bc->bc_key->crypto_cfg.data_unit_size;
	unsigned int i;
	blk_status_t blk_st;

	/*
	 * Get a blk-crypto-fallback keyslot that contains a crypto_skcipher for
	 * this bio's algorithm and key.
	 */
	blk_st = blk_crypto_get_keyslot(blk_crypto_fallback_profile,
					bc->bc_key, &slot);
	if (blk_st != BLK_STS_OK) {
		bio->bi_status = blk_st;
		goto out_no_keyslot;
	}

	/* and then allocate an skcipher_request for it */
	if (!blk_crypto_fallback_alloc_cipher_req(slot, &ciph_req, &wait)) {
		bio->bi_status = BLK_STS_RESOURCE;
		goto out;
	}

	memcpy(curr_dun, bc->bc_dun, sizeof(curr_dun));
	sg_init_table(&sg, 1);
	skcipher_request_set_crypt(ciph_req, &sg, &sg, data_unit_size,
				   iv.bytes);

	/* Decrypt each segment in the bio */
	__bio_for_each_segment(bv, bio, iter, f_ctx->crypt_iter) {
		struct page *page = bv.bv_page;

		sg_set_page(&sg, page, data_unit_size, bv.bv_offset);

		/* Decrypt each data unit in the segment */
		for (i = 0; i < bv.bv_len; i += data_unit_size) {
			blk_crypto_dun_to_iv(curr_dun, &iv);
			if (crypto_wait_req(crypto_skcipher_decrypt(ciph_req),
					    &wait)) {
				bio->bi_status = BLK_STS_IOERR;
				goto out;
			}
			bio_crypt_dun_increment(curr_dun, 1);
			sg.offset += data_unit_size;
		}
	}

out:
	skcipher_request_free(ciph_req);
	blk_crypto_put_keyslot(slot);
out_no_keyslot:
	mempool_free(f_ctx, bio_fallback_crypt_ctx_pool);
	bio_endio(bio);
}

/**
 * blk_crypto_fallback_decrypt_endio - queue bio for fallback decryption
 *
 * @bio: the bio to queue
 *
 * Restore bi_private and bi_end_io, and queue the bio for decryption into a
 * workqueue, since this function will be called from an atomic context.
 */
static void blk_crypto_fallback_decrypt_endio(struct bio *bio)
{
	struct bio_fallback_crypt_ctx *f_ctx = bio->bi_private;

	bio->bi_private = f_ctx->bi_private_orig;
	bio->bi_end_io = f_ctx->bi_end_io_orig;

	/* If there was an IO error, don't queue for decrypt. */
	if (bio->bi_status) {
		mempool_free(f_ctx, bio_fallback_crypt_ctx_pool);
		bio_endio(bio);
		return;
	}

	INIT_WORK(&f_ctx->work, blk_crypto_fallback_decrypt_bio);
	f_ctx->bio = bio;
	queue_work(blk_crypto_wq, &f_ctx->work);
}

/**
 * blk_crypto_fallback_bio_prep - Prepare a bio to use fallback en/decryption
 *
 * @bio_ptr: pointer to the bio to prepare
 *
 * If bio is doing a WRITE operation, this splits the bio into two parts if it's
 * too big (see blk_crypto_fallback_split_bio_if_needed()). It then allocates a
 * bounce bio for the first part, encrypts it, and updates bio_ptr to point to
 * the bounce bio.
 *
 * For a READ operation, we mark the bio for decryption by using bi_private and
 * bi_end_io.
 *
 * In either case, this function will make the bio look like a regular bio (i.e.
 * as if no encryption context was ever specified) for the purposes of the rest
 * of the stack except for blk-integrity (blk-integrity and blk-crypto are not
 * currently supported together).
 *
 * Return: true on success. Sets bio->bi_status and returns false on error.
 */
bool blk_crypto_fallback_bio_prep(struct bio **bio_ptr)
{
	struct bio *bio = *bio_ptr;
	struct bio_crypt_ctx *bc = bio->bi_crypt_context;
	struct bio_fallback_crypt_ctx *f_ctx;

	if (WARN_ON_ONCE(!tfms_inited[bc->bc_key->crypto_cfg.crypto_mode])) {
		/* User didn't call blk_crypto_start_using_key() first */
		bio->bi_status = BLK_STS_IOERR;
		return false;
	}

	if (!__blk_crypto_cfg_supported(blk_crypto_fallback_profile,
					&bc->bc_key->crypto_cfg)) {
		bio->bi_status = BLK_STS_NOTSUPP;
		return false;
	}

	if (bio_data_dir(bio) == WRITE)
		return blk_crypto_fallback_encrypt_bio(bio_ptr);

	/*
	 * bio READ case: Set up a f_ctx in the bio's bi_private and set the
	 * bi_end_io appropriately to trigger decryption when the bio is ended.
	 */
	f_ctx = mempool_alloc(bio_fallback_crypt_ctx_pool, GFP_NOIO);
	f_ctx->crypt_ctx = *bc;
	f_ctx->crypt_iter = bio->bi_iter;
	f_ctx->bi_private_orig = bio->bi_private;
	f_ctx->bi_end_io_orig = bio->bi_end_io;
	bio->bi_private = (void *)f_ctx;
	bio->bi_end_io = blk_crypto_fallback_decrypt_endio;
	bio_crypt_free_ctx(bio);

	return true;
}

int blk_crypto_fallback_evict_key(const struct blk_crypto_key *key)
{
	return __blk_crypto_evict_key(blk_crypto_fallback_profile, key);
}

static bool blk_crypto_fallback_inited;
static int blk_crypto_fallback_init(void)
{
	int i;
	int err;

	if (blk_crypto_fallback_inited)
		return 0;

	get_random_bytes(blank_key, sizeof(blank_key));

	err = bioset_init(&crypto_bio_split, 64, 0, 0);
	if (err)
		goto out;

	/* Dynamic allocation is needed because of lockdep_register_key(). */
	blk_crypto_fallback_profile =
		kzalloc(sizeof(*blk_crypto_fallback_profile), GFP_KERNEL);
	if (!blk_crypto_fallback_profile) {
		err = -ENOMEM;
		goto fail_free_bioset;
	}

	err = blk_crypto_profile_init(blk_crypto_fallback_profile,
				      blk_crypto_num_keyslots);
	if (err)
		goto fail_free_profile;
	err = -ENOMEM;

	blk_crypto_fallback_profile->ll_ops = blk_crypto_fallback_ll_ops;
	blk_crypto_fallback_profile->max_dun_bytes_supported = BLK_CRYPTO_MAX_IV_SIZE;
	blk_crypto_fallback_profile->key_types_supported = BLK_CRYPTO_KEY_TYPE_STANDARD;

	/* All blk-crypto modes have a crypto API fallback. */
	for (i = 0; i < BLK_ENCRYPTION_MODE_MAX; i++)
		blk_crypto_fallback_profile->modes_supported[i] = 0xFFFFFFFF;
	blk_crypto_fallback_profile->modes_supported[BLK_ENCRYPTION_MODE_INVALID] = 0;

	blk_crypto_wq = alloc_workqueue("blk_crypto_wq",
					WQ_UNBOUND | WQ_HIGHPRI |
					WQ_MEM_RECLAIM, num_online_cpus());
	if (!blk_crypto_wq)
		goto fail_destroy_profile;

	blk_crypto_keyslots = kcalloc(blk_crypto_num_keyslots,
				      sizeof(blk_crypto_keyslots[0]),
				      GFP_KERNEL);
	if (!blk_crypto_keyslots)
		goto fail_free_wq;

	blk_crypto_bounce_page_pool =
		mempool_create_page_pool(num_prealloc_bounce_pg, 0);
	if (!blk_crypto_bounce_page_pool)
		goto fail_free_keyslots;

	bio_fallback_crypt_ctx_cache = KMEM_CACHE(bio_fallback_crypt_ctx, 0);
	if (!bio_fallback_crypt_ctx_cache)
		goto fail_free_bounce_page_pool;

	bio_fallback_crypt_ctx_pool =
		mempool_create_slab_pool(num_prealloc_fallback_crypt_ctxs,
					 bio_fallback_crypt_ctx_cache);
	if (!bio_fallback_crypt_ctx_pool)
		goto fail_free_crypt_ctx_cache;

	blk_crypto_fallback_inited = true;

	return 0;
fail_free_crypt_ctx_cache:
	kmem_cache_destroy(bio_fallback_crypt_ctx_cache);
fail_free_bounce_page_pool:
	mempool_destroy(blk_crypto_bounce_page_pool);
fail_free_keyslots:
	kfree(blk_crypto_keyslots);
fail_free_wq:
	destroy_workqueue(blk_crypto_wq);
fail_destroy_profile:
	blk_crypto_profile_destroy(blk_crypto_fallback_profile);
fail_free_profile:
	kfree(blk_crypto_fallback_profile);
fail_free_bioset:
	bioset_exit(&crypto_bio_split);
out:
	return err;
}

/*
 * Prepare blk-crypto-fallback for the specified crypto mode.
 * Returns -ENOPKG if the needed crypto API support is missing.
 */
int blk_crypto_fallback_start_using_mode(enum blk_crypto_mode_num mode_num)
{
	const char *cipher_str = blk_crypto_modes[mode_num].cipher_str;
	struct blk_crypto_fallback_keyslot *slotp;
	unsigned int i;
	int err = 0;

	/*
	 * Fast path
	 * Ensure that updates to blk_crypto_keyslots[i].tfms[mode_num]
	 * for each i are visible before we try to access them.
	 */
	if (likely(smp_load_acquire(&tfms_inited[mode_num])))
		return 0;

	mutex_lock(&tfms_init_lock);
	if (tfms_inited[mode_num])
		goto out;

	err = blk_crypto_fallback_init();
	if (err)
		goto out;

	for (i = 0; i < blk_crypto_num_keyslots; i++) {
		slotp = &blk_crypto_keyslots[i];
		slotp->tfms[mode_num] = crypto_alloc_skcipher(cipher_str, 0, 0);
		if (IS_ERR(slotp->tfms[mode_num])) {
			err = PTR_ERR(slotp->tfms[mode_num]);
			if (err == -ENOENT) {
				pr_warn_once("Missing crypto API support for \"%s\"\n",
					     cipher_str);
				err = -ENOPKG;
			}
			slotp->tfms[mode_num] = NULL;
			goto out_free_tfms;
		}

		crypto_skcipher_set_flags(slotp->tfms[mode_num],
					  CRYPTO_TFM_REQ_FORBID_WEAK_KEYS);
	}

	/*
	 * Ensure that updates to blk_crypto_keyslots[i].tfms[mode_num]
	 * for each i are visible before we set tfms_inited[mode_num].
	 */
	smp_store_release(&tfms_inited[mode_num], true);
	goto out;

out_free_tfms:
	for (i = 0; i < blk_crypto_num_keyslots; i++) {
		slotp = &blk_crypto_keyslots[i];
		crypto_free_skcipher(slotp->tfms[mode_num]);
		slotp->tfms[mode_num] = NULL;
	}
out:
	mutex_unlock(&tfms_init_lock);
	return err;
}
