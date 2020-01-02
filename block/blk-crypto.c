// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

/*
 * Refer to Documentation/block/inline-encryption.rst for detailed explanation.
 */

#define pr_fmt(fmt) "blk-crypto: " fmt

#include <linux/blk-crypto.h>
#include <linux/blkdev.h>
#include <linux/keyslot-manager.h>
#include <linux/random.h>
#include <linux/siphash.h>

#include "blk-crypto-internal.h"

const struct blk_crypto_mode blk_crypto_modes[] = {
	[BLK_ENCRYPTION_MODE_AES_256_XTS] = {
		.cipher_str = "xts(aes)",
		.keysize = 64,
		.ivsize = 16,
	},
	[BLK_ENCRYPTION_MODE_AES_128_CBC_ESSIV] = {
		.cipher_str = "essiv(cbc(aes),sha256)",
		.keysize = 16,
		.ivsize = 16,
	},
	[BLK_ENCRYPTION_MODE_ADIANTUM] = {
		.cipher_str = "adiantum(xchacha12,aes)",
		.keysize = 32,
		.ivsize = 32,
	},
};

/* Check that all I/O segments are data unit aligned */
static int bio_crypt_check_alignment(struct bio *bio)
{
	const unsigned int data_unit_size =
				bio->bi_crypt_context->bc_key->data_unit_size;
	struct bvec_iter iter;
	struct bio_vec bv;

	bio_for_each_segment(bv, bio, iter) {
		if (!IS_ALIGNED(bv.bv_len | bv.bv_offset, data_unit_size))
			return -EIO;
	}
	return 0;
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
	struct bio_crypt_ctx *bc = bio->bi_crypt_context;
	int err;

	if (!bc || !bio_has_data(bio))
		return 0;

	/*
	 * When a read bio is marked for fallback decryption, its bi_iter is
	 * saved so that when we decrypt the bio later, we know what part of it
	 * was marked for fallback decryption (when the bio is passed down after
	 * blk_crypto_submit bio, it may be split or advanced so we cannot rely
	 * on the bi_iter while decrypting in blk_crypto_endio)
	 */
	if (bio_crypt_fallback_crypted(bc))
		return 0;

	err = bio_crypt_check_alignment(bio);
	if (err) {
		bio->bi_status = BLK_STS_IOERR;
		goto out;
	}

	q = bio->bi_disk->queue;

	if (bc->bc_ksm) {
		/* Key already programmed into device? */
		if (q->ksm == bc->bc_ksm)
			return 0;

		/* Nope, release the existing keyslot. */
		bio_crypt_ctx_release_keyslot(bc);
	}

	/* Get device keyslot if supported */
	if (keyslot_manager_crypto_mode_supported(q->ksm,
						  bc->bc_key->crypto_mode,
						  bc->bc_key->data_unit_size)) {
		err = bio_crypt_ctx_acquire_keyslot(bc, q->ksm);
		if (!err)
			return 0;

		pr_warn_once("Failed to acquire keyslot for %s (err=%d).  Falling back to crypto API.\n",
			     bio->bi_disk->disk_name, err);
	}

	/* Fallback to crypto API */
	err = blk_crypto_fallback_submit_bio(bio_ptr);
	if (err)
		goto out;

	return 0;
out:
	bio_endio(*bio_ptr);
	return err;
}

/**
 * blk_crypto_endio - clean up bio w.r.t inline encryption during bio_endio
 *
 * @bio: the bio to clean up
 *
 * If blk_crypto_submit_bio decided to fallback to crypto API for this bio,
 * we queue the bio for decryption into a workqueue and return false,
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
	struct bio_crypt_ctx *bc = bio->bi_crypt_context;

	if (!bc)
		return true;

	if (bio_crypt_fallback_crypted(bc)) {
		/*
		 * The only bios who's crypto is handled by the blk-crypto
		 * fallback when they reach here are those with
		 * bio_data_dir(bio) == READ, since WRITE bios that are
		 * encrypted by the crypto API fallback are handled by
		 * blk_crypto_encrypt_endio().
		 */
		return !blk_crypto_queue_decrypt_bio(bio);
	}

	if (bc->bc_keyslot >= 0)
		bio_crypt_ctx_release_keyslot(bc);

	return true;
}

/**
 * blk_crypto_init_key() - Prepare a key for use with blk-crypto
 * @blk_key: Pointer to the blk_crypto_key to initialize.
 * @raw_key: Pointer to the raw key.
 * @raw_key_size: Size of raw key.  Must be at least the required size for the
 *                chosen @crypto_mode; see blk_crypto_modes[].  (It's allowed
 *                to be longer than the mode's actual key size, in order to
 *                support inline encryption hardware that accepts wrapped keys.)
 * @crypto_mode: identifier for the encryption algorithm to use
 * @data_unit_size: the data unit size to use for en/decryption
 *
 * Return: The blk_crypto_key that was prepared, or an ERR_PTR() on error.  When
 *	   done using the key, it must be freed with blk_crypto_free_key().
 */
int blk_crypto_init_key(struct blk_crypto_key *blk_key,
			const u8 *raw_key, unsigned int raw_key_size,
			enum blk_crypto_mode_num crypto_mode,
			unsigned int data_unit_size)
{
	const struct blk_crypto_mode *mode;
	static siphash_key_t hash_key;

	memset(blk_key, 0, sizeof(*blk_key));

	if (crypto_mode >= ARRAY_SIZE(blk_crypto_modes))
		return -EINVAL;

	BUILD_BUG_ON(BLK_CRYPTO_MAX_WRAPPED_KEY_SIZE < BLK_CRYPTO_MAX_KEY_SIZE);

	mode = &blk_crypto_modes[crypto_mode];
	if (raw_key_size < mode->keysize ||
	    raw_key_size > BLK_CRYPTO_MAX_WRAPPED_KEY_SIZE)
		return -EINVAL;

	if (!is_power_of_2(data_unit_size))
		return -EINVAL;

	blk_key->crypto_mode = crypto_mode;
	blk_key->data_unit_size = data_unit_size;
	blk_key->data_unit_size_bits = ilog2(data_unit_size);
	blk_key->size = raw_key_size;
	memcpy(blk_key->raw, raw_key, raw_key_size);

	/*
	 * The keyslot manager uses the SipHash of the key to implement O(1) key
	 * lookups while avoiding leaking information about the keys.  It's
	 * precomputed here so that it only needs to be computed once per key.
	 */
	get_random_once(&hash_key, sizeof(hash_key));
	blk_key->hash = siphash(raw_key, raw_key_size, &hash_key);

	return 0;
}

/**
 * blk_crypto_evict_key() - Evict a key from any inline encryption hardware
 *			    it may have been programmed into
 * @q: The request queue who's keyslot manager this key might have been
 *     programmed into
 * @key: The key to evict
 *
 * Upper layers (filesystems) should call this function to ensure that a key
 * is evicted from hardware that it might have been programmed into. This
 * will call keyslot_manager_evict_key on the queue's keyslot manager, if one
 * exists, and supports the crypto algorithm with the specified data unit size.
 * Otherwise, it will evict the key from the blk-crypto-fallback's ksm.
 *
 * Return: 0 on success, -err on error.
 */
int blk_crypto_evict_key(struct request_queue *q,
			 const struct blk_crypto_key *key)
{
	if (q->ksm &&
	    keyslot_manager_crypto_mode_supported(q->ksm, key->crypto_mode,
						  key->data_unit_size))
		return keyslot_manager_evict_key(q->ksm, key);

	return blk_crypto_fallback_evict_key(key);
}
