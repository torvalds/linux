// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

/*
 * Refer to Documentation/block/inline-encryption.rst for detailed explanation.
 */

#define pr_fmt(fmt) "blk-crypto: " fmt

#include <linux/blk-crypto.h>
#include <linux/keyslot-manager.h>
#include <linux/mempool.h>
#include <linux/blk-cgroup.h>
#include <linux/crypto.h>
#include <crypto/skcipher.h>
#include <crypto/algapi.h>
#include <linux/module.h>
#include <linux/sched/mm.h>

/* Represents a crypto mode supported by blk-crypto  */
struct blk_crypto_mode {
	const char *cipher_str; /* crypto API name (for fallback case) */
	size_t keysize; /* key size in bytes */
};

static const struct blk_crypto_mode blk_crypto_modes[] = {
	[BLK_ENCRYPTION_MODE_AES_256_XTS] = {
		.cipher_str = "xts(aes)",
		.keysize = 64,
	},
};

static unsigned int num_prealloc_bounce_pg = 32;
module_param(num_prealloc_bounce_pg, uint, 0);
MODULE_PARM_DESC(num_prealloc_bounce_pg,
	"Number of preallocated bounce pages for blk-crypto to use during crypto API fallback encryption");

#define BLK_CRYPTO_MAX_KEY_SIZE 64
static int blk_crypto_num_keyslots = 100;
module_param_named(num_keyslots, blk_crypto_num_keyslots, int, 0);
MODULE_PARM_DESC(num_keyslots,
		 "Number of keyslots for crypto API fallback in blk-crypto.");

static struct blk_crypto_keyslot {
	struct crypto_skcipher *tfm;
	enum blk_crypto_mode_num crypto_mode;
	u8 key[BLK_CRYPTO_MAX_KEY_SIZE];
	struct crypto_skcipher *tfms[ARRAY_SIZE(blk_crypto_modes)];
} *blk_crypto_keyslots;

/*
 * Allocating a crypto tfm during I/O can deadlock, so we have to preallocate
 * all of a mode's tfms when that mode starts being used. Since each mode may
 * need all the keyslots at some point, each mode needs its own tfm for each
 * keyslot; thus, a keyslot may contain tfms for multiple modes.  However, to
 * match the behavior of real inline encryption hardware (which only supports a
 * single encryption context per keyslot), we only allow one tfm per keyslot to
 * be used at a time - the rest of the unused tfms have their keys cleared.
 */
static struct mutex tfms_lock[ARRAY_SIZE(blk_crypto_modes)];
static bool tfms_inited[ARRAY_SIZE(blk_crypto_modes)];

struct work_mem {
	struct work_struct crypto_work;
	struct bio *bio;
};

/* The following few vars are only used during the crypto API fallback */
static struct keyslot_manager *blk_crypto_ksm;
static struct workqueue_struct *blk_crypto_wq;
static mempool_t *blk_crypto_page_pool;
static struct kmem_cache *blk_crypto_work_mem_cache;

bool bio_crypt_swhandled(struct bio *bio)
{
	return bio_has_crypt_ctx(bio) &&
	       bio->bi_crypt_context->processing_ksm == blk_crypto_ksm;
}

static u8 blank_key[BLK_CRYPTO_MAX_KEY_SIZE];
static void evict_keyslot(unsigned int slot)
{
	struct blk_crypto_keyslot *slotp = &blk_crypto_keyslots[slot];
	enum blk_crypto_mode_num crypto_mode = slotp->crypto_mode;
	int err;

	WARN_ON(slotp->crypto_mode == BLK_ENCRYPTION_MODE_INVALID);

	/* Clear the key in the skcipher */
	err = crypto_skcipher_setkey(slotp->tfms[crypto_mode], blank_key,
				     blk_crypto_modes[crypto_mode].keysize);
	WARN_ON(err);
	memzero_explicit(slotp->key, BLK_CRYPTO_MAX_KEY_SIZE);
	slotp->crypto_mode = BLK_ENCRYPTION_MODE_INVALID;
}

static int blk_crypto_keyslot_program(void *priv, const u8 *key,
				      enum blk_crypto_mode_num crypto_mode,
				      unsigned int data_unit_size,
				      unsigned int slot)
{
	struct blk_crypto_keyslot *slotp = &blk_crypto_keyslots[slot];
	const struct blk_crypto_mode *mode = &blk_crypto_modes[crypto_mode];
	size_t keysize = mode->keysize;
	int err;

	if (crypto_mode != slotp->crypto_mode &&
	    slotp->crypto_mode != BLK_ENCRYPTION_MODE_INVALID) {
		evict_keyslot(slot);
	}

	if (!slotp->tfms[crypto_mode])
		return -ENOMEM;
	slotp->crypto_mode = crypto_mode;
	err = crypto_skcipher_setkey(slotp->tfms[crypto_mode], key, keysize);

	if (err) {
		evict_keyslot(slot);
		return err;
	}

	memcpy(slotp->key, key, keysize);

	return 0;
}

static int blk_crypto_keyslot_evict(void *priv, const u8 *key,
				    enum blk_crypto_mode_num crypto_mode,
				    unsigned int data_unit_size,
				    unsigned int slot)
{
	evict_keyslot(slot);
	return 0;
}

static int blk_crypto_keyslot_find(void *priv,
				   const u8 *key,
				   enum blk_crypto_mode_num crypto_mode,
				   unsigned int data_unit_size_bytes)
{
	int slot;
	const size_t keysize = blk_crypto_modes[crypto_mode].keysize;

	for (slot = 0; slot < blk_crypto_num_keyslots; slot++) {
		if (blk_crypto_keyslots[slot].crypto_mode == crypto_mode &&
		    !crypto_memneq(blk_crypto_keyslots[slot].key, key, keysize))
			return slot;
	}

	return -ENOKEY;
}

static bool blk_crypto_mode_supported(void *priv,
				      enum blk_crypto_mode_num crypt_mode,
				      unsigned int data_unit_size)
{
	/* All blk_crypto_modes are required to have a crypto API fallback. */
	return true;
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
	.keyslot_find		= blk_crypto_keyslot_find,
	.crypto_mode_supported	= blk_crypto_mode_supported,
};

static void blk_crypto_encrypt_endio(struct bio *enc_bio)
{
	struct bio *src_bio = enc_bio->bi_private;
	int i;

	for (i = 0; i < enc_bio->bi_vcnt; i++)
		mempool_free(enc_bio->bi_io_vec[i].bv_page,
			     blk_crypto_page_pool);

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

	bio_clone_blkg_association(bio, bio_src);
	blkcg_bio_issue_init(bio);

	return bio;
}

/* Check that all I/O segments are data unit aligned */
static int bio_crypt_check_alignment(struct bio *bio)
{
	int data_unit_size = 1 << bio->bi_crypt_context->data_unit_size_bits;
	struct bvec_iter iter;
	struct bio_vec bv;

	bio_for_each_segment(bv, bio, iter) {
		if (!IS_ALIGNED(bv.bv_len | bv.bv_offset, data_unit_size))
			return -EIO;
	}
	return 0;
}

static int blk_crypto_alloc_cipher_req(struct bio *src_bio,
				       struct skcipher_request **ciph_req_ptr,
				       struct crypto_wait *wait)
{
	int slot;
	struct skcipher_request *ciph_req;
	struct blk_crypto_keyslot *slotp;

	slot = bio_crypt_get_keyslot(src_bio);
	slotp = &blk_crypto_keyslots[slot];
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
	*ciph_req_ptr = ciph_req;
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

/*
 * The crypto API fallback's encryption routine.
 * Allocate a bounce bio for encryption, encrypt the input bio using
 * crypto API, and replace *bio_ptr with the bounce bio. May split input
 * bio if it's too large.
 */
static int blk_crypto_encrypt_bio(struct bio **bio_ptr)
{
	struct bio *src_bio;
	struct skcipher_request *ciph_req = NULL;
	DECLARE_CRYPTO_WAIT(wait);
	int err = 0;
	u64 curr_dun;
	union {
		__le64 dun;
		u8 bytes[16];
	} iv;
	struct scatterlist src, dst;
	struct bio *enc_bio;
	struct bio_vec *enc_bvec;
	int i, j;
	int data_unit_size;

	/* Split the bio if it's too big for single page bvec */
	err = blk_crypto_split_bio_if_needed(bio_ptr);
	if (err)
		return err;

	src_bio = *bio_ptr;
	data_unit_size = 1 << src_bio->bi_crypt_context->data_unit_size_bits;

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
	err = bio_crypt_ctx_acquire_keyslot(src_bio, blk_crypto_ksm);
	if (err) {
		src_bio->bi_status = BLK_STS_IOERR;
		goto out_put_enc_bio;
	}

	/* and then allocate an skcipher_request for it */
	err = blk_crypto_alloc_cipher_req(src_bio, &ciph_req, &wait);
	if (err)
		goto out_release_keyslot;

	curr_dun = bio_crypt_data_unit_num(src_bio);
	sg_init_table(&src, 1);
	sg_init_table(&dst, 1);

	skcipher_request_set_crypt(ciph_req, &src, &dst,
				   data_unit_size, iv.bytes);

	/* Encrypt each page in the bounce bio */
	for (i = 0, enc_bvec = enc_bio->bi_io_vec; i < enc_bio->bi_vcnt;
	     enc_bvec++, i++) {
		struct page *plaintext_page = enc_bvec->bv_page;
		struct page *ciphertext_page =
			mempool_alloc(blk_crypto_page_pool, GFP_NOIO);

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
			memset(&iv, 0, sizeof(iv));
			iv.dun = cpu_to_le64(curr_dun);

			err = crypto_wait_req(crypto_skcipher_encrypt(ciph_req),
					      &wait);
			if (err) {
				i++;
				src_bio->bi_status = BLK_STS_RESOURCE;
				goto out_free_bounce_pages;
			}
			curr_dun++;
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
			     blk_crypto_page_pool);
out_free_ciph_req:
	skcipher_request_free(ciph_req);
out_release_keyslot:
	bio_crypt_ctx_release_keyslot(src_bio);
out_put_enc_bio:
	if (enc_bio)
		bio_put(enc_bio);

	return err;
}

/*
 * The crypto API fallback's main decryption routine.
 * Decrypts input bio in place.
 */
static void blk_crypto_decrypt_bio(struct work_struct *w)
{
	struct work_mem *work_mem =
		container_of(w, struct work_mem, crypto_work);
	struct bio *bio = work_mem->bio;
	struct skcipher_request *ciph_req = NULL;
	DECLARE_CRYPTO_WAIT(wait);
	struct bio_vec bv;
	struct bvec_iter iter;
	u64 curr_dun;
	union {
		__le64 dun;
		u8 bytes[16];
	} iv;
	struct scatterlist sg;
	int data_unit_size = 1 << bio->bi_crypt_context->data_unit_size_bits;
	int i;
	int err;

	/*
	 * Use the crypto API fallback keyslot manager to get a crypto_skcipher
	 * for the algorithm and key specified for this bio.
	 */
	if (bio_crypt_ctx_acquire_keyslot(bio, blk_crypto_ksm)) {
		bio->bi_status = BLK_STS_RESOURCE;
		goto out_no_keyslot;
	}

	/* and then allocate an skcipher_request for it */
	err = blk_crypto_alloc_cipher_req(bio, &ciph_req, &wait);
	if (err)
		goto out;

	curr_dun = bio_crypt_sw_data_unit_num(bio);
	sg_init_table(&sg, 1);
	skcipher_request_set_crypt(ciph_req, &sg, &sg, data_unit_size,
				   iv.bytes);

	/* Decrypt each segment in the bio */
	__bio_for_each_segment(bv, bio, iter,
			       bio->bi_crypt_context->crypt_iter) {
		struct page *page = bv.bv_page;

		sg_set_page(&sg, page, data_unit_size, bv.bv_offset);

		/* Decrypt each data unit in the segment */
		for (i = 0; i < bv.bv_len; i += data_unit_size) {
			memset(&iv, 0, sizeof(iv));
			iv.dun = cpu_to_le64(curr_dun);
			if (crypto_wait_req(crypto_skcipher_decrypt(ciph_req),
					    &wait)) {
				bio->bi_status = BLK_STS_IOERR;
				goto out;
			}
			curr_dun++;
			sg.offset += data_unit_size;
		}
	}

out:
	skcipher_request_free(ciph_req);
	bio_crypt_ctx_release_keyslot(bio);
out_no_keyslot:
	kmem_cache_free(blk_crypto_work_mem_cache, work_mem);
	bio_endio(bio);
}

/* Queue bio for decryption */
static void blk_crypto_queue_decrypt_bio(struct bio *bio)
{
	struct work_mem *work_mem =
		kmem_cache_zalloc(blk_crypto_work_mem_cache, GFP_ATOMIC);

	if (!work_mem) {
		bio->bi_status = BLK_STS_RESOURCE;
		bio_endio(bio);
		return;
	}

	INIT_WORK(&work_mem->crypto_work, blk_crypto_decrypt_bio);
	work_mem->bio = bio;
	queue_work(blk_crypto_wq, &work_mem->crypto_work);
}

/**
 * blk_crypto_submit_bio - handle submitting bio for inline encryption
 *
 * @bio_ptr: pointer to original bio pointer
 *
 * If the bio doesn't have inline encryption enabled or the submitter already
 * specified a keyslot for the target device, do nothing.  Else, a raw key must
 * have been provided, so acquire a device keyslot for it if supported.  Else,
 * use the crypto API fallback.
 *
 * When the crypto API fallback is used for encryption, blk-crypto may choose to
 * split the bio into 2 - the first one that will continue to be processed and
 * the second one that will be resubmitted via generic_make_request.
 * A bounce bio will be allocated to encrypt the contents of the aforementioned
 * "first one", and *bio_ptr will be updated to this bounce bio.
 *
 * Return: 0 if bio submission should continue; nonzero if bio_endio() was
 *	   already called so bio submission should abort.
 */
int blk_crypto_submit_bio(struct bio **bio_ptr)
{
	struct bio *bio = *bio_ptr;
	struct request_queue *q;
	int err;
	struct bio_crypt_ctx *crypt_ctx;

	if (!bio_has_crypt_ctx(bio) || !bio_has_data(bio))
		return 0;

	/*
	 * When a read bio is marked for sw decryption, its bi_iter is saved
	 * so that when we decrypt the bio later, we know what part of it was
	 * marked for sw decryption (when the bio is passed down after
	 * blk_crypto_submit bio, it may be split or advanced so we cannot rely
	 * on the bi_iter while decrypting in blk_crypto_endio)
	 */
	if (bio_crypt_swhandled(bio))
		return 0;

	err = bio_crypt_check_alignment(bio);
	if (err) {
		bio->bi_status = BLK_STS_IOERR;
		goto out;
	}

	crypt_ctx = bio->bi_crypt_context;
	q = bio->bi_disk->queue;

	if (bio_crypt_has_keyslot(bio)) {
		/* Key already programmed into device? */
		if (q->ksm == crypt_ctx->processing_ksm)
			return 0;

		/* Nope, release the existing keyslot. */
		bio_crypt_ctx_release_keyslot(bio);
	}

	/* Get device keyslot if supported */
	if (q->ksm) {
		err = bio_crypt_ctx_acquire_keyslot(bio, q->ksm);
		if (!err)
			return 0;

		pr_warn_once("Failed to acquire keyslot for %s (err=%d).  Falling back to crypto API.\n",
			     bio->bi_disk->disk_name, err);
	}

	/* Fallback to crypto API */
	if (!READ_ONCE(tfms_inited[bio->bi_crypt_context->crypto_mode])) {
		err = -EIO;
		bio->bi_status = BLK_STS_IOERR;
		goto out;
	}

	if (bio_data_dir(bio) == WRITE) {
		/* Encrypt the data now */
		err = blk_crypto_encrypt_bio(bio_ptr);
		if (err)
			goto out;
	} else {
		/* Mark bio as swhandled */
		bio->bi_crypt_context->processing_ksm = blk_crypto_ksm;
		bio->bi_crypt_context->crypt_iter = bio->bi_iter;
		bio->bi_crypt_context->sw_data_unit_num =
				bio->bi_crypt_context->data_unit_num;
	}
	return 0;
out:
	bio_endio(*bio_ptr);
	return err;
}

/**
 * blk_crypto_endio - clean up bio w.r.t inline encryption during bio_endio
 *
 * @bio - the bio to clean up
 *
 * If blk_crypto_submit_bio decided to fallback to crypto API for this
 * bio, we queue the bio for decryption into a workqueue and return false,
 * and call bio_endio(bio) at a later time (after the bio has been decrypted).
 *
 * If the bio is not to be decrypted by the crypto API, this function releases
 * the reference to the keyslot that blk_crypto_submit_bio got.
 *
 * Return: true if bio_endio should continue; false otherwise (bio_endio will
 * be called again when bio has been decrypted).
 */
bool blk_crypto_endio(struct bio *bio)
{
	if (!bio_has_crypt_ctx(bio))
		return true;

	if (bio_crypt_swhandled(bio)) {
		/*
		 * The only bios that are swhandled when they reach here
		 * are those with bio_data_dir(bio) == READ, since WRITE
		 * bios that are encrypted by the crypto API fallback are
		 * handled by blk_crypto_encrypt_endio.
		 */

		/* If there was an IO error, don't decrypt. */
		if (bio->bi_status)
			return true;

		blk_crypto_queue_decrypt_bio(bio);
		return false;
	}

	if (bio_crypt_has_keyslot(bio))
		bio_crypt_ctx_release_keyslot(bio);

	return true;
}

/**
 * blk_crypto_start_using_mode() - Allocate skciphers for a
 *				   mode_num for all keyslots
 * @mode_num - the blk_crypto_mode we want to allocate ciphers for.
 *
 * Upper layers (filesystems) should call this function to ensure that a
 * the crypto API fallback has transforms for this algorithm, if they become
 * necessary.
 *
 * Return: 0 on success and -err on error.
 */
int blk_crypto_start_using_mode(enum blk_crypto_mode_num mode_num,
				unsigned int data_unit_size,
				struct request_queue *q)
{
	struct blk_crypto_keyslot *slotp;
	int err = 0;
	int i;

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
						  data_unit_size)) {
		return 0;
	}

	mutex_lock(&tfms_lock[mode_num]);
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
	mutex_unlock(&tfms_lock[mode_num]);
	return err;
}
EXPORT_SYMBOL(blk_crypto_start_using_mode);

/**
 * blk_crypto_evict_key() - Evict a key from any inline encryption hardware
 *			    it may have been programmed into
 * @q - The request queue who's keyslot manager this key might have been
 *	programmed into
 * @key - The key to evict
 * @mode - The blk_crypto_mode_num used with this key
 * @data_unit_size - The data unit size used with this key
 *
 * Upper layers (filesystems) should call this function to ensure that a key
 * is evicted from hardware that it might have been programmed into. This
 * will call keyslot_manager_evict_key on the queue's keyslot manager, if one
 * exists, and supports the crypto algorithm with the specified data unit size.
 * Otherwise, it will evict the key from the blk_crypto_ksm.
 *
 * Return: 0 on success, -err on error.
 */
int blk_crypto_evict_key(struct request_queue *q, const u8 *key,
			 enum blk_crypto_mode_num mode,
			 unsigned int data_unit_size)
{
	struct keyslot_manager *ksm = blk_crypto_ksm;

	if (q && q->ksm && keyslot_manager_crypto_mode_supported(q->ksm, mode,
							    data_unit_size)) {
		ksm = q->ksm;
	}

	return keyslot_manager_evict_key(ksm, key, mode, data_unit_size);
}
EXPORT_SYMBOL(blk_crypto_evict_key);

int __init blk_crypto_init(void)
{
	int i;
	int err = -ENOMEM;

	prandom_bytes(blank_key, BLK_CRYPTO_MAX_KEY_SIZE);

	blk_crypto_ksm = keyslot_manager_create(blk_crypto_num_keyslots,
						&blk_crypto_ksm_ll_ops,
						NULL);
	if (!blk_crypto_ksm)
		goto out;

	blk_crypto_wq = alloc_workqueue("blk_crypto_wq",
					WQ_UNBOUND | WQ_HIGHPRI |
					WQ_MEM_RECLAIM,
					num_online_cpus());
	if (!blk_crypto_wq)
		goto out_free_ksm;

	blk_crypto_keyslots = kcalloc(blk_crypto_num_keyslots,
				      sizeof(*blk_crypto_keyslots),
				      GFP_KERNEL);
	if (!blk_crypto_keyslots)
		goto out_free_workqueue;

	for (i = 0; i < blk_crypto_num_keyslots; i++) {
		blk_crypto_keyslots[i].crypto_mode =
						BLK_ENCRYPTION_MODE_INVALID;
	}

	for (i = 0; i < ARRAY_SIZE(blk_crypto_modes); i++)
		mutex_init(&tfms_lock[i]);

	blk_crypto_page_pool =
		mempool_create_page_pool(num_prealloc_bounce_pg, 0);
	if (!blk_crypto_page_pool)
		goto out_free_keyslots;

	blk_crypto_work_mem_cache = KMEM_CACHE(work_mem, SLAB_RECLAIM_ACCOUNT);
	if (!blk_crypto_work_mem_cache)
		goto out_free_page_pool;

	return 0;

out_free_page_pool:
	mempool_destroy(blk_crypto_page_pool);
	blk_crypto_page_pool = NULL;
out_free_keyslots:
	kzfree(blk_crypto_keyslots);
	blk_crypto_keyslots = NULL;
out_free_workqueue:
	destroy_workqueue(blk_crypto_wq);
	blk_crypto_wq = NULL;
out_free_ksm:
	keyslot_manager_destroy(blk_crypto_ksm);
	blk_crypto_ksm = NULL;
out:
	pr_warn("No memory for blk-crypto crypto API fallback.");
	return err;
}
