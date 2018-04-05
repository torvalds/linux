// SPDX-License-Identifier: GPL-2.0
/*
 * Speck: a lightweight block cipher
 *
 * Copyright (c) 2018 Google, Inc
 *
 * Speck has 10 variants, including 5 block sizes.  For now we only implement
 * the variants Speck128/128, Speck128/192, Speck128/256, Speck64/96, and
 * Speck64/128.   Speck${B}/${K} denotes the variant with a block size of B bits
 * and a key size of K bits.  The Speck128 variants are believed to be the most
 * secure variants, and they use the same block size and key sizes as AES.  The
 * Speck64 variants are less secure, but on 32-bit processors are usually
 * faster.  The remaining variants (Speck32, Speck48, and Speck96) are even less
 * secure and/or not as well suited for implementation on either 32-bit or
 * 64-bit processors, so are omitted.
 *
 * Reference: "The Simon and Speck Families of Lightweight Block Ciphers"
 * https://eprint.iacr.org/2013/404.pdf
 *
 * In a correspondence, the Speck designers have also clarified that the words
 * should be interpreted in little-endian format, and the words should be
 * ordered such that the first word of each block is 'y' rather than 'x', and
 * the first key word (rather than the last) becomes the first round key.
 */

#include <asm/unaligned.h>
#include <crypto/speck.h>
#include <linux/bitops.h>
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/module.h>

/* Speck128 */

static __always_inline void speck128_round(u64 *x, u64 *y, u64 k)
{
	*x = ror64(*x, 8);
	*x += *y;
	*x ^= k;
	*y = rol64(*y, 3);
	*y ^= *x;
}

static __always_inline void speck128_unround(u64 *x, u64 *y, u64 k)
{
	*y ^= *x;
	*y = ror64(*y, 3);
	*x ^= k;
	*x -= *y;
	*x = rol64(*x, 8);
}

void crypto_speck128_encrypt(const struct speck128_tfm_ctx *ctx,
			     u8 *out, const u8 *in)
{
	u64 y = get_unaligned_le64(in);
	u64 x = get_unaligned_le64(in + 8);
	int i;

	for (i = 0; i < ctx->nrounds; i++)
		speck128_round(&x, &y, ctx->round_keys[i]);

	put_unaligned_le64(y, out);
	put_unaligned_le64(x, out + 8);
}
EXPORT_SYMBOL_GPL(crypto_speck128_encrypt);

static void speck128_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	crypto_speck128_encrypt(crypto_tfm_ctx(tfm), out, in);
}

void crypto_speck128_decrypt(const struct speck128_tfm_ctx *ctx,
			     u8 *out, const u8 *in)
{
	u64 y = get_unaligned_le64(in);
	u64 x = get_unaligned_le64(in + 8);
	int i;

	for (i = ctx->nrounds - 1; i >= 0; i--)
		speck128_unround(&x, &y, ctx->round_keys[i]);

	put_unaligned_le64(y, out);
	put_unaligned_le64(x, out + 8);
}
EXPORT_SYMBOL_GPL(crypto_speck128_decrypt);

static void speck128_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	crypto_speck128_decrypt(crypto_tfm_ctx(tfm), out, in);
}

int crypto_speck128_setkey(struct speck128_tfm_ctx *ctx, const u8 *key,
			   unsigned int keylen)
{
	u64 l[3];
	u64 k;
	int i;

	switch (keylen) {
	case SPECK128_128_KEY_SIZE:
		k = get_unaligned_le64(key);
		l[0] = get_unaligned_le64(key + 8);
		ctx->nrounds = SPECK128_128_NROUNDS;
		for (i = 0; i < ctx->nrounds; i++) {
			ctx->round_keys[i] = k;
			speck128_round(&l[0], &k, i);
		}
		break;
	case SPECK128_192_KEY_SIZE:
		k = get_unaligned_le64(key);
		l[0] = get_unaligned_le64(key + 8);
		l[1] = get_unaligned_le64(key + 16);
		ctx->nrounds = SPECK128_192_NROUNDS;
		for (i = 0; i < ctx->nrounds; i++) {
			ctx->round_keys[i] = k;
			speck128_round(&l[i % 2], &k, i);
		}
		break;
	case SPECK128_256_KEY_SIZE:
		k = get_unaligned_le64(key);
		l[0] = get_unaligned_le64(key + 8);
		l[1] = get_unaligned_le64(key + 16);
		l[2] = get_unaligned_le64(key + 24);
		ctx->nrounds = SPECK128_256_NROUNDS;
		for (i = 0; i < ctx->nrounds; i++) {
			ctx->round_keys[i] = k;
			speck128_round(&l[i % 3], &k, i);
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_speck128_setkey);

static int speck128_setkey(struct crypto_tfm *tfm, const u8 *key,
			   unsigned int keylen)
{
	return crypto_speck128_setkey(crypto_tfm_ctx(tfm), key, keylen);
}

/* Speck64 */

static __always_inline void speck64_round(u32 *x, u32 *y, u32 k)
{
	*x = ror32(*x, 8);
	*x += *y;
	*x ^= k;
	*y = rol32(*y, 3);
	*y ^= *x;
}

static __always_inline void speck64_unround(u32 *x, u32 *y, u32 k)
{
	*y ^= *x;
	*y = ror32(*y, 3);
	*x ^= k;
	*x -= *y;
	*x = rol32(*x, 8);
}

void crypto_speck64_encrypt(const struct speck64_tfm_ctx *ctx,
			    u8 *out, const u8 *in)
{
	u32 y = get_unaligned_le32(in);
	u32 x = get_unaligned_le32(in + 4);
	int i;

	for (i = 0; i < ctx->nrounds; i++)
		speck64_round(&x, &y, ctx->round_keys[i]);

	put_unaligned_le32(y, out);
	put_unaligned_le32(x, out + 4);
}
EXPORT_SYMBOL_GPL(crypto_speck64_encrypt);

static void speck64_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	crypto_speck64_encrypt(crypto_tfm_ctx(tfm), out, in);
}

void crypto_speck64_decrypt(const struct speck64_tfm_ctx *ctx,
			    u8 *out, const u8 *in)
{
	u32 y = get_unaligned_le32(in);
	u32 x = get_unaligned_le32(in + 4);
	int i;

	for (i = ctx->nrounds - 1; i >= 0; i--)
		speck64_unround(&x, &y, ctx->round_keys[i]);

	put_unaligned_le32(y, out);
	put_unaligned_le32(x, out + 4);
}
EXPORT_SYMBOL_GPL(crypto_speck64_decrypt);

static void speck64_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	crypto_speck64_decrypt(crypto_tfm_ctx(tfm), out, in);
}

int crypto_speck64_setkey(struct speck64_tfm_ctx *ctx, const u8 *key,
			  unsigned int keylen)
{
	u32 l[3];
	u32 k;
	int i;

	switch (keylen) {
	case SPECK64_96_KEY_SIZE:
		k = get_unaligned_le32(key);
		l[0] = get_unaligned_le32(key + 4);
		l[1] = get_unaligned_le32(key + 8);
		ctx->nrounds = SPECK64_96_NROUNDS;
		for (i = 0; i < ctx->nrounds; i++) {
			ctx->round_keys[i] = k;
			speck64_round(&l[i % 2], &k, i);
		}
		break;
	case SPECK64_128_KEY_SIZE:
		k = get_unaligned_le32(key);
		l[0] = get_unaligned_le32(key + 4);
		l[1] = get_unaligned_le32(key + 8);
		l[2] = get_unaligned_le32(key + 12);
		ctx->nrounds = SPECK64_128_NROUNDS;
		for (i = 0; i < ctx->nrounds; i++) {
			ctx->round_keys[i] = k;
			speck64_round(&l[i % 3], &k, i);
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_speck64_setkey);

static int speck64_setkey(struct crypto_tfm *tfm, const u8 *key,
			  unsigned int keylen)
{
	return crypto_speck64_setkey(crypto_tfm_ctx(tfm), key, keylen);
}

/* Algorithm definitions */

static struct crypto_alg speck_algs[] = {
	{
		.cra_name		= "speck128",
		.cra_driver_name	= "speck128-generic",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
		.cra_blocksize		= SPECK128_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct speck128_tfm_ctx),
		.cra_module		= THIS_MODULE,
		.cra_u			= {
			.cipher = {
				.cia_min_keysize	= SPECK128_128_KEY_SIZE,
				.cia_max_keysize	= SPECK128_256_KEY_SIZE,
				.cia_setkey		= speck128_setkey,
				.cia_encrypt		= speck128_encrypt,
				.cia_decrypt		= speck128_decrypt
			}
		}
	}, {
		.cra_name		= "speck64",
		.cra_driver_name	= "speck64-generic",
		.cra_priority		= 100,
		.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
		.cra_blocksize		= SPECK64_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct speck64_tfm_ctx),
		.cra_module		= THIS_MODULE,
		.cra_u			= {
			.cipher = {
				.cia_min_keysize	= SPECK64_96_KEY_SIZE,
				.cia_max_keysize	= SPECK64_128_KEY_SIZE,
				.cia_setkey		= speck64_setkey,
				.cia_encrypt		= speck64_encrypt,
				.cia_decrypt		= speck64_decrypt
			}
		}
	}
};

static int __init speck_module_init(void)
{
	return crypto_register_algs(speck_algs, ARRAY_SIZE(speck_algs));
}

static void __exit speck_module_exit(void)
{
	crypto_unregister_algs(speck_algs, ARRAY_SIZE(speck_algs));
}

module_init(speck_module_init);
module_exit(speck_module_exit);

MODULE_DESCRIPTION("Speck block cipher (generic)");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eric Biggers <ebiggers@google.com>");
MODULE_ALIAS_CRYPTO("speck128");
MODULE_ALIAS_CRYPTO("speck128-generic");
MODULE_ALIAS_CRYPTO("speck64");
MODULE_ALIAS_CRYPTO("speck64-generic");
