// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

/*
 * Refer to Documentation/block/inline-encryption.rst for detailed explanation.
 */

#define pr_fmt(fmt) "blk-crypto-fallback: " fmt

#include <crypto/skcipher.h>
#include <linux/blk-cgroup.h>
#include <linux/blk-crypto.h>
#include <linux/crypto.h>
#include <linux/keyslot-manager.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/random.h>

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
	u64 fallback_dun[BLK_CRYPTO_DUN_ARRAY_SIZE];
};

/* The following few vars are only used during the crypto API fallback */
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

struct blk_crypto_decrypt_work {
	struct work_struct work;
	struct bio *bio;
};

static struct blk_crypto_keyslot {
	struct crypto_skcipher *tfm;
	enum blk_crypto_mode_num crypto_mode;
	struct crypto_skcipher *tfms[BLK_ENCRYPTION_MODE_MAX];
} *blk_crypto_keyslots;

/* The following few vars are only used during the crypto API fallback */
static struct keyslot_manager *blk_crypto_ksm;
static struct workqueue_struct *blk_crypto_wq;
static mempool_t *blk_crypto_bounce_page_pool;
static struct kmem_cache *blk_crypto_decrypt_work_cache;

bool bio_crypt_fallback_crypted(const struct bio_crypt_ctx *bc)
{
	return bc && bc->bc_ksm == blk_crypto_ksm;
}

/*
 * This is the key we set when evicting a keyslot. This *should* be the all 0's
 * key, but AES-XTS rejects that key, so we use some random bytes instead.
 */
static u8 blank_key[BLK_CRYPTO_MAX_KEY_SIZE];

static void blk_crypto_evict_keyslot(unsigned int slot)
{
	struct blk_crypto_keyslot *slotp = &blk_crypto_keyslots[slot];
	enum blk_crypto_mode_num crypto_mode = slotp->crypto_mode;
	int err;

	WARN_ON(slotp->crypto_mode == BLK_ENCRYPTION_MODE_INVALID);

	/* Clear the key in the skcipher */
	err = crypto_skcipher_setkey(slotp->tfms[crypto_mode], blank_key,
				     blk_crypto_modes[crypto_mode].keysize);
	WARN_ON(err);
	slotp->crypto_mode = BLK_ENCRYPTION_MODE_INVALID;
}

static int blk_crypto_keyslot_program(struct keyslot_manager *ksm,
				      const struct blk_crypto_key *key,
				      unsigned int slot)
{
	struct blk_crypto_keyslot *slotp = &blk_crypto_keyslots[slot];
	const enum blk_crypto_mode_num crypto_mode = key->crypto_mode;
	int err;

	if (crypto_mode != slotp->crypto_mode &&
	    slotp->crypto_mode != BLK_ENCRYPTION_MODE_INVALID) {
		blk_crypto_evict_keyslot(slot);
	}

	if (!slotp->tfms[crypto_mode])
		return -ENOMEM;
	slotp->crypto_mode = crypto_mode;
	err = crypto_skcipher_setkey(slotp->tfms[crypto_mode], key->raw,
				     key->size);
	if (err) {
		blk_crypto_evict_keyslot(slot);
		return err;
	}
	return 0;
}

static int blk_crypto_keyslot_evict(struct keyslot_manager *ksm,
				    const struct blk_crypto_key *key,
				    unsigned int slot)
{
	blk_crypto_evict_keyslot(slot);
	return 0;
}

/*
 * The crypto API fallback KSM ops - only used for a bio when it specifies a
 * blk_crypto_mode for which we failed to get a keyslot in the device's inline
 * encryption hardware (which probably means the device doesn't have inline
 * encryption hardware that supports that crypto mode).
 */
static const struct keyslot_mgmt_ll_ops blk_crypto_ksm_ll_ops = {
	.keyslot_program	= blk_crypto_keyslot_program,
	.keyslot_evict		= blk_crypto_keyslot_evict,
};

static void blk_crypto_encrypt_endio(struct bio *enc_bio)
{
	struct bio *src_bio = enc_bio->bi_private;
	int i;

	for (i = 0; i < enc_bio->bi_vcnt; i++)
		mempool_free(enc_bio->bi_io_vec[i].bv_page,
			     blk_crypto_bounce_page_pool);

	src_bio->bi_status = enc_bio->bi_status;

	bio_put(enc_bio);
	bio_endio(src_bio);
}

static struct bio *blk_crypto_clone_bio(struct bio *bio_src)
{
	struct bvec_iter iter;
	struct bio_vec bv;
	struct bio *bio;

	bio = bio_alloc_bioset(GFP_NOIO, bio_segments(bio_src), NULL);
	if (!bio)
		return NULL;
	bio->bi_disk		= bio_src->bi_disk;
	bio->bi_opf		= bio_src->bi_opf;
	bio->bi_ioprio		= bio_src->bi_ioprio;
	bio->bi_write_hint	= bio_src->bi_write_hint;
	bio->bi_iter.bi_sector	= bio_src->bi_iter.bi_sector;
	bio->bi_iter.bi_size	= bio_src->bi_iter.bi_size;

	bio_for_each_segment(bv, bio_src, iter)
		bio->bi_io_vec[bio->bi_vcnt++] = bv;

	if (bio_integrity(bio_src) &&
	    bio_integrity_clone(bio, bio_src, GFP_NOIO) < 0) {
		bio_put(bio);
		return NULL;
	}

	bio_clone_blkcg_association(bio, bio_src);

	bio_clone_skip_dm_default_key(bio, bio_src);

	return bio;
}

static int blk_crypto_alloc_cipher_req(struct bio *src_bio,
				       struct skcipher_request **ciph_req_ret,
				       struct crypto_wait *wait)
{
	struct skcipher_request *ciph_req;
	const struct blk_crypto_keyslot *slotp;

	slotp = &blk_crypto_keyslots[src_bio->bi_crypt_context->bc_keyslot];
	ciph_req = skcipher_request_alloc(slotp->tfms[slotp->crypto_mode],
					  GFP_NOIO);
	if (!ciph_req) {
		src_bio->bi_status = BLK_STS_RESOURCE;
		return -ENOMEM;
	}

	skcipher_request_set_callback(ciph_req,
				      CRYPTO_TFM_REQ_MAY_BACKLOG |
				      CRYPTO_TFM_REQ_MAY_SLEEP,
				      crypto_req_done, wait);
	*ciph_req_ret = ciph_req;
	return 0;
}

static int blk_crypto_split_bio_if_needed(struct bio **bio_ptr)
{
	struct bio *bio = *bio_ptr;
	unsigned int i = 0;
	unsigned int num_sectors = 0;
	struct bio_vec bv;
	struct bvec_iter iter;

	bio_for_each_segment(bv, bio, iter) {
		num_sectors += bv.bv_len >> SECTOR_SHIFT;
		if (++i == BIO_MAX_PAGES)
			break;
	}
	if (num_sectors < bio_sectors(bio)) {
		struct bio *split_bio;

		split_bio = bio_split(bio, num_sectors, GFP_NOIO, NULL);
		if (!split_bio) {
			bio->bi_status = BLK_STS_RESOURCE;
			return -ENOMEM;
		}
		bio_chain(split_bio, bio);
		generic_make_request(bio);
		*bio_ptr = split_bio;
	}
	return 0;
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
 * large.
 */
static int blk_crypto_encrypt_bio(struct bio **bio_ptr)
{
	struct bio *src_bio;
	struct skcipher_request *ciph_req = NULL;
	DECLARE_CRYPTO_WAIT(wait);
	u64 curr_dun[BLK_CRYPTO_DUN_ARRAY_SIZE];
	union blk_crypto_iv iv;
	struct scatterlist src, dst;
	struct bio *enc_bio;
	unsigned int i, j;
	int data_unit_size;
	struct bio_crypt_ctx *bc;
	int err = 0;

	/* Split the bio if it's too big for single page bvec */
	err = blk_crypto_split_bio_if_needed(bio_ptr);
	if (err)
		return err;

	src_bio = *bio_ptr;
	bc = src_bio->bi_crypt_context;
	data_unit_size = bc->bc_key->data_unit_size;

	/* Allocate bounce bio for encryption */
	enc_bio = blk_crypto_clone_bio(src_bio);
	if (!enc_bio) {
		src_bio->bi_status = BLK_STS_RESOURCE;
		return -ENOMEM;
	}

	/*
	 * Use the crypto API fallback keyslot manager to get a crypto_skcipher
	 * for the algorithm and key specified for this bio.
	 */
	err = bio_crypt_ctx_acquire_keyslot(bc, blk_crypto_ksm);
	if (err) {
		src_bio->bi_status = BLK_STS_IOERR;
		goto out_put_enc_bio;
	}

	/* and then allocate an skcipher_request for it */
	err = blk_crypto_alloc_cipher_req(src_bio, &ciph_req, &wait);
	if (err)
		goto out_release_keyslot;

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
			err = -ENOMEM;
			goto out_free_bounce_pages;
		}

		sg_set_page(&src, plaintext_page, data_unit_size,
			    enc_bvec->bv_offset);
		sg_set_page(&dst, ciphertext_page, data_unit_size,
			    enc_bvec->bv_offset);

		/* Encrypt each data unit in this page */
		for (j = 0; j < enc_bvec->bv_len; j += data_unit_size) {
			blk_crypto_dun_to_iv(curr_dun, &iv);
			err = crypto_wait_req(crypto_skcipher_encrypt(ciph_req),
					      &wait);
			if (err) {
				i++;
				src_bio->bi_status = BLK_STS_RESOURCE;
				goto out_free_bounce_pages;
			}
			bio_crypt_dun_increment(curr_dun, 1);
			src.offset += data_unit_size;
			dst.offset += data_unit_size;
		}
	}

	enc_bio->bi_private = src_bio;
	enc_bio->bi_end_io = blk_crypto_encrypt_endio;
	*bio_ptr = enc_bio;

	enc_bio = NULL;
	err = 0;
	goto out_free_ciph_req;

out_free_bounce_pages:
	while (i > 0)
		mempool_free(enc_bio->bi_io_vec[--i].bv_page,
			     blk_crypto_bounce_page_pool);
out_free_ciph_req:
	skcipher_request_free(ciph_req);
out_release_keyslot:
	bio_crypt_ctx_release_keyslot(bc);
out_put_enc_bio:
	if (enc_bio)
		bio_put(enc_bio);

	return err;
}

static void blk_crypto_free_fallback_crypt_ctx(struct bio *bio)
{
	mempool_free(container_of(bio->bi_crypt_context,
				  struct bio_fallback_crypt_ctx,
				  crypt_ctx),
		     bio_fallback_crypt_ctx_pool);
	bio->bi_crypt_context = NULL;
}

/*
 * The crypto API fallback's main decryption routine.
 * Decrypts input bio in place.
 */
static void blk_crypto_decrypt_bio(struct work_struct *work)
{
	struct blk_crypto_decrypt_work *decrypt_work =
		container_of(work, struct blk_crypto_decrypt_work, work);
	struct bio *bio = decrypt_work->bio;
	struct skcipher_request *ciph_req = NULL;
	DECLARE_CRYPTO_WAIT(wait);
	struct bio_vec bv;
	struct bvec_iter iter;
	u64 curr_dun[BLK_CRYPTO_DUN_ARRAY_SIZE];
	union blk_crypto_iv iv;
	struct scatterlist sg;
	struct bio_crypt_ctx *bc = bio->bi_crypt_context;
	struct bio_fallback_crypt_ctx *f_ctx =
		container_of(bc, struct bio_fallback_crypt_ctx, crypt_ctx);
	const int data_unit_size = bc->bc_key->data_unit_size;
	unsigned int i;
	int err;

	/*
	 * Use the crypto API fallback keyslot manager to get a crypto_skcipher
	 * for the algorithm and key specified for this bio.
	 */
	if (bio_crypt_ctx_acquire_keyslot(bc, blk_crypto_ksm)) {
		bio->bi_status = BLK_STS_RESOURCE;
		goto out_no_keyslot;
	}

	/* and then allocate an skcipher_request for it */
	err = blk_crypto_alloc_cipher_req(bio, &ciph_req, &wait);
	if (err)
		goto out;

	memcpy(curr_dun, f_ctx->fallback_dun, sizeof(curr_dun));
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
	bio_crypt_ctx_release_keyslot(bc);
out_no_keyslot:
	kmem_cache_free(blk_crypto_decrypt_work_cache, decrypt_work);
	blk_crypto_free_fallback_crypt_ctx(bio);
	bio_endio(bio);
}

/*
 * Queue bio for decryption.
 * Returns true iff bio was queued for decryption.
 */
bool blk_crypto_queue_decrypt_bio(struct bio *bio)
{
	struct blk_crypto_decrypt_work *decrypt_work;

	/* If there was an IO error, don't queue for decrypt. */
	if (bio->bi_status)
		goto out;

	decrypt_work = kmem_cache_zalloc(blk_crypto_decrypt_work_cache,
					 GFP_ATOMIC);
	if (!decrypt_work) {
		bio->bi_status = BLK_STS_RESOURCE;
		goto out;
	}

	INIT_WORK(&decrypt_work->work, blk_crypto_decrypt_bio);
	decrypt_work->bio = bio;
	queue_work(blk_crypto_wq, &decrypt_work->work);

	return true;
out:
	blk_crypto_free_fallback_crypt_ctx(bio);
	return false;
}

/**
 * blk_crypto_start_using_mode() - Start using a crypto algorithm on a device
 * @mode_num: the blk_crypto_mode we want to allocate ciphers for.
 * @data_unit_size: the data unit size that will be used
 * @q: the request queue for the device
 *
 * Upper layers must call this function to ensure that a the crypto API fallback
 * has transforms for this algorithm, if they become necessary.
 *
 * Return: 0 on success and -err on error.
 */
int blk_crypto_start_using_mode(enum blk_crypto_mode_num mode_num,
				unsigned int data_unit_size,
				struct request_queue *q)
{
	struct blk_crypto_keyslot *slotp;
	unsigned int i;
	int err = 0;

	/*
	 * Fast path
	 * Ensure that updates to blk_crypto_keyslots[i].tfms[mode_num]
	 * for each i are visible before we try to access them.
	 */
	if (likely(smp_load_acquire(&tfms_inited[mode_num])))
		return 0;

	/*
	 * If the keyslot manager of the request queue supports this
	 * crypto mode, then we don't need to allocate this mode.
	 */
	if (keyslot_manager_crypto_mode_supported(q->ksm, mode_num,
						  data_unit_size))
		return 0;

	mutex_lock(&tfms_init_lock);
	if (likely(tfms_inited[mode_num]))
		goto out;

	for (i = 0; i < blk_crypto_num_keyslots; i++) {
		slotp = &blk_crypto_keyslots[i];
		slotp->tfms[mode_num] = crypto_alloc_skcipher(
					blk_crypto_modes[mode_num].cipher_str,
					0, 0);
		if (IS_ERR(slotp->tfms[mode_num])) {
			err = PTR_ERR(slotp->tfms[mode_num]);
			slotp->tfms[mode_num] = NULL;
			goto out_free_tfms;
		}

		crypto_skcipher_set_flags(slotp->tfms[mode_num],
					  CRYPTO_TFM_REQ_WEAK_KEY);
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
EXPORT_SYMBOL_GPL(blk_crypto_start_using_mode);

int blk_crypto_fallback_evict_key(const struct blk_crypto_key *key)
{
	return keyslot_manager_evict_key(blk_crypto_ksm, key);
}

int blk_crypto_fallback_submit_bio(struct bio **bio_ptr)
{
	struct bio *bio = *bio_ptr;
	struct bio_crypt_ctx *bc = bio->bi_crypt_context;
	struct bio_fallback_crypt_ctx *f_ctx;

	if (bc->bc_key->is_hw_wrapped) {
		pr_warn_once("HW wrapped key cannot be used with fallback.\n");
		bio->bi_status = BLK_STS_NOTSUPP;
		return -EOPNOTSUPP;
	}

	if (!tfms_inited[bc->bc_key->crypto_mode]) {
		bio->bi_status = BLK_STS_IOERR;
		return -EIO;
	}

	if (bio_data_dir(bio) == WRITE)
		return blk_crypto_encrypt_bio(bio_ptr);

	/*
	 * Mark bio as fallback crypted and replace the bio_crypt_ctx with
	 * another one contained in a bio_fallback_crypt_ctx, so that the
	 * fallback has space to store the info it needs for decryption.
	 */
	bc->bc_ksm = blk_crypto_ksm;
	f_ctx = mempool_alloc(bio_fallback_crypt_ctx_pool, GFP_NOIO);
	f_ctx->crypt_ctx = *bc;
	memcpy(f_ctx->fallback_dun, bc->bc_dun, sizeof(f_ctx->fallback_dun));
	f_ctx->crypt_iter = bio->bi_iter;

	bio_crypt_free_ctx(bio);
	bio->bi_crypt_context = &f_ctx->crypt_ctx;

	return 0;
}

int __init blk_crypto_fallback_init(void)
{
	int i;
	unsigned int crypto_mode_supported[BLK_ENCRYPTION_MODE_MAX];

	prandom_bytes(blank_key, BLK_CRYPTO_MAX_KEY_SIZE);

	/* All blk-crypto modes have a crypto API fallback. */
	for (i = 0; i < BLK_ENCRYPTION_MODE_MAX; i++)
		crypto_mode_supported[i] = 0xFFFFFFFF;
	crypto_mode_supported[BLK_ENCRYPTION_MODE_INVALID] = 0;

	blk_crypto_ksm = keyslot_manager_create(NULL, blk_crypto_num_keyslots,
						&blk_crypto_ksm_ll_ops,
						crypto_mode_supported, NULL);
	if (!blk_crypto_ksm)
		return -ENOMEM;

	blk_crypto_wq = alloc_workqueue("blk_crypto_wq",
					WQ_UNBOUND | WQ_HIGHPRI |
					WQ_MEM_RECLAIM, num_online_cpus());
	if (!blk_crypto_wq)
		return -ENOMEM;

	blk_crypto_keyslots = kcalloc(blk_crypto_num_keyslots,
				      sizeof(blk_crypto_keyslots[0]),
				      GFP_KERNEL);
	if (!blk_crypto_keyslots)
		return -ENOMEM;

	blk_crypto_bounce_page_pool =
		mempool_create_page_pool(num_prealloc_bounce_pg, 0);
	if (!blk_crypto_bounce_page_pool)
		return -ENOMEM;

	blk_crypto_decrypt_work_cache = KMEM_CACHE(blk_crypto_decrypt_work,
						   SLAB_RECLAIM_ACCOUNT);
	if (!blk_crypto_decrypt_work_cache)
		return -ENOMEM;

	bio_fallback_crypt_ctx_cache = KMEM_CACHE(bio_fallback_crypt_ctx, 0);
	if (!bio_fallback_crypt_ctx_cache)
		return -ENOMEM;

	bio_fallback_crypt_ctx_pool =
		mempool_create_slab_pool(num_prealloc_fallback_crypt_ctxs,
					 bio_fallback_crypt_ctx_cache);
	if (!bio_fallback_crypt_ctx_pool)
		return -ENOMEM;

	return 0;
}
