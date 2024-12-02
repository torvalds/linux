// SPDX-License-Identifier: GPL-2.0
/*
 * NHPoly1305 - ε-almost-∆-universal hash function for Adiantum
 *
 * Copyright 2018 Google LLC
 */

/*
 * "NHPoly1305" is the main component of Adiantum hashing.
 * Specifically, it is the calculation
 *
 *	H_L ← Poly1305_{K_L}(NH_{K_N}(pad_{128}(L)))
 *
 * from the procedure in section 6.4 of the Adiantum paper [1].  It is an
 * ε-almost-∆-universal (ε-∆U) hash function for equal-length inputs over
 * Z/(2^{128}Z), where the "∆" operation is addition.  It hashes 1024-byte
 * chunks of the input with the NH hash function [2], reducing the input length
 * by 32x.  The resulting NH digests are evaluated as a polynomial in
 * GF(2^{130}-5), like in the Poly1305 MAC [3].  Note that the polynomial
 * evaluation by itself would suffice to achieve the ε-∆U property; NH is used
 * for performance since it's over twice as fast as Poly1305.
 *
 * This is *not* a cryptographic hash function; do not use it as such!
 *
 * [1] Adiantum: length-preserving encryption for entry-level processors
 *     (https://eprint.iacr.org/2018/720.pdf)
 * [2] UMAC: Fast and Secure Message Authentication
 *     (https://fastcrypto.org/umac/umac_proc.pdf)
 * [3] The Poly1305-AES message-authentication code
 *     (https://cr.yp.to/mac/poly1305-20050329.pdf)
 */

#include <asm/unaligned.h>
#include <crypto/algapi.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/poly1305.h>
#include <crypto/nhpoly1305.h>
#include <linux/crypto.h>
#include <linux/kernel.h>
#include <linux/module.h>

static void nh_generic(const u32 *key, const u8 *message, size_t message_len,
		       __le64 hash[NH_NUM_PASSES])
{
	u64 sums[4] = { 0, 0, 0, 0 };

	BUILD_BUG_ON(NH_PAIR_STRIDE != 2);
	BUILD_BUG_ON(NH_NUM_PASSES != 4);

	while (message_len) {
		u32 m0 = get_unaligned_le32(message + 0);
		u32 m1 = get_unaligned_le32(message + 4);
		u32 m2 = get_unaligned_le32(message + 8);
		u32 m3 = get_unaligned_le32(message + 12);

		sums[0] += (u64)(u32)(m0 + key[ 0]) * (u32)(m2 + key[ 2]);
		sums[1] += (u64)(u32)(m0 + key[ 4]) * (u32)(m2 + key[ 6]);
		sums[2] += (u64)(u32)(m0 + key[ 8]) * (u32)(m2 + key[10]);
		sums[3] += (u64)(u32)(m0 + key[12]) * (u32)(m2 + key[14]);
		sums[0] += (u64)(u32)(m1 + key[ 1]) * (u32)(m3 + key[ 3]);
		sums[1] += (u64)(u32)(m1 + key[ 5]) * (u32)(m3 + key[ 7]);
		sums[2] += (u64)(u32)(m1 + key[ 9]) * (u32)(m3 + key[11]);
		sums[3] += (u64)(u32)(m1 + key[13]) * (u32)(m3 + key[15]);
		key += NH_MESSAGE_UNIT / sizeof(key[0]);
		message += NH_MESSAGE_UNIT;
		message_len -= NH_MESSAGE_UNIT;
	}

	hash[0] = cpu_to_le64(sums[0]);
	hash[1] = cpu_to_le64(sums[1]);
	hash[2] = cpu_to_le64(sums[2]);
	hash[3] = cpu_to_le64(sums[3]);
}

/* Pass the next NH hash value through Poly1305 */
static void process_nh_hash_value(struct nhpoly1305_state *state,
				  const struct nhpoly1305_key *key)
{
	BUILD_BUG_ON(NH_HASH_BYTES % POLY1305_BLOCK_SIZE != 0);

	poly1305_core_blocks(&state->poly_state, &key->poly_key, state->nh_hash,
			     NH_HASH_BYTES / POLY1305_BLOCK_SIZE, 1);
}

/*
 * Feed the next portion of the source data, as a whole number of 16-byte
 * "NH message units", through NH and Poly1305.  Each NH hash is taken over
 * 1024 bytes, except possibly the final one which is taken over a multiple of
 * 16 bytes up to 1024.  Also, in the case where data is passed in misaligned
 * chunks, we combine partial hashes; the end result is the same either way.
 */
static void nhpoly1305_units(struct nhpoly1305_state *state,
			     const struct nhpoly1305_key *key,
			     const u8 *src, unsigned int srclen, nh_t nh_fn)
{
	do {
		unsigned int bytes;

		if (state->nh_remaining == 0) {
			/* Starting a new NH message */
			bytes = min_t(unsigned int, srclen, NH_MESSAGE_BYTES);
			nh_fn(key->nh_key, src, bytes, state->nh_hash);
			state->nh_remaining = NH_MESSAGE_BYTES - bytes;
		} else {
			/* Continuing a previous NH message */
			__le64 tmp_hash[NH_NUM_PASSES];
			unsigned int pos;
			int i;

			pos = NH_MESSAGE_BYTES - state->nh_remaining;
			bytes = min(srclen, state->nh_remaining);
			nh_fn(&key->nh_key[pos / 4], src, bytes, tmp_hash);
			for (i = 0; i < NH_NUM_PASSES; i++)
				le64_add_cpu(&state->nh_hash[i],
					     le64_to_cpu(tmp_hash[i]));
			state->nh_remaining -= bytes;
		}
		if (state->nh_remaining == 0)
			process_nh_hash_value(state, key);
		src += bytes;
		srclen -= bytes;
	} while (srclen);
}

int crypto_nhpoly1305_setkey(struct crypto_shash *tfm,
			     const u8 *key, unsigned int keylen)
{
	struct nhpoly1305_key *ctx = crypto_shash_ctx(tfm);
	int i;

	if (keylen != NHPOLY1305_KEY_SIZE)
		return -EINVAL;

	poly1305_core_setkey(&ctx->poly_key, key);
	key += POLY1305_BLOCK_SIZE;

	for (i = 0; i < NH_KEY_WORDS; i++)
		ctx->nh_key[i] = get_unaligned_le32(key + i * sizeof(u32));

	return 0;
}
EXPORT_SYMBOL(crypto_nhpoly1305_setkey);

int crypto_nhpoly1305_init(struct shash_desc *desc)
{
	struct nhpoly1305_state *state = shash_desc_ctx(desc);

	poly1305_core_init(&state->poly_state);
	state->buflen = 0;
	state->nh_remaining = 0;
	return 0;
}
EXPORT_SYMBOL(crypto_nhpoly1305_init);

int crypto_nhpoly1305_update_helper(struct shash_desc *desc,
				    const u8 *src, unsigned int srclen,
				    nh_t nh_fn)
{
	struct nhpoly1305_state *state = shash_desc_ctx(desc);
	const struct nhpoly1305_key *key = crypto_shash_ctx(desc->tfm);
	unsigned int bytes;

	if (state->buflen) {
		bytes = min(srclen, (int)NH_MESSAGE_UNIT - state->buflen);
		memcpy(&state->buffer[state->buflen], src, bytes);
		state->buflen += bytes;
		if (state->buflen < NH_MESSAGE_UNIT)
			return 0;
		nhpoly1305_units(state, key, state->buffer, NH_MESSAGE_UNIT,
				 nh_fn);
		state->buflen = 0;
		src += bytes;
		srclen -= bytes;
	}

	if (srclen >= NH_MESSAGE_UNIT) {
		bytes = round_down(srclen, NH_MESSAGE_UNIT);
		nhpoly1305_units(state, key, src, bytes, nh_fn);
		src += bytes;
		srclen -= bytes;
	}

	if (srclen) {
		memcpy(state->buffer, src, srclen);
		state->buflen = srclen;
	}
	return 0;
}
EXPORT_SYMBOL(crypto_nhpoly1305_update_helper);

int crypto_nhpoly1305_update(struct shash_desc *desc,
			     const u8 *src, unsigned int srclen)
{
	return crypto_nhpoly1305_update_helper(desc, src, srclen, nh_generic);
}
EXPORT_SYMBOL(crypto_nhpoly1305_update);

int crypto_nhpoly1305_final_helper(struct shash_desc *desc, u8 *dst, nh_t nh_fn)
{
	struct nhpoly1305_state *state = shash_desc_ctx(desc);
	const struct nhpoly1305_key *key = crypto_shash_ctx(desc->tfm);

	if (state->buflen) {
		memset(&state->buffer[state->buflen], 0,
		       NH_MESSAGE_UNIT - state->buflen);
		nhpoly1305_units(state, key, state->buffer, NH_MESSAGE_UNIT,
				 nh_fn);
	}

	if (state->nh_remaining)
		process_nh_hash_value(state, key);

	poly1305_core_emit(&state->poly_state, NULL, dst);
	return 0;
}
EXPORT_SYMBOL(crypto_nhpoly1305_final_helper);

int crypto_nhpoly1305_final(struct shash_desc *desc, u8 *dst)
{
	return crypto_nhpoly1305_final_helper(desc, dst, nh_generic);
}
EXPORT_SYMBOL(crypto_nhpoly1305_final);

static struct shash_alg nhpoly1305_alg = {
	.base.cra_name		= "nhpoly1305",
	.base.cra_driver_name	= "nhpoly1305-generic",
	.base.cra_priority	= 100,
	.base.cra_ctxsize	= sizeof(struct nhpoly1305_key),
	.base.cra_module	= THIS_MODULE,
	.digestsize		= POLY1305_DIGEST_SIZE,
	.init			= crypto_nhpoly1305_init,
	.update			= crypto_nhpoly1305_update,
	.final			= crypto_nhpoly1305_final,
	.setkey			= crypto_nhpoly1305_setkey,
	.descsize		= sizeof(struct nhpoly1305_state),
};

static int __init nhpoly1305_mod_init(void)
{
	return crypto_register_shash(&nhpoly1305_alg);
}

static void __exit nhpoly1305_mod_exit(void)
{
	crypto_unregister_shash(&nhpoly1305_alg);
}

subsys_initcall(nhpoly1305_mod_init);
module_exit(nhpoly1305_mod_exit);

MODULE_DESCRIPTION("NHPoly1305 ε-almost-∆-universal hash function");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Eric Biggers <ebiggers@google.com>");
MODULE_ALIAS_CRYPTO("nhpoly1305");
MODULE_ALIAS_CRYPTO("nhpoly1305-generic");
