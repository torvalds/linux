// SPDX-License-Identifier: GPL-2.0
/*
 * NEON-accelerated implementation of Speck128-XTS and Speck64-XTS
 *
 * Copyright (c) 2018 Google, Inc
 *
 * Note: the NIST recommendation for XTS only specifies a 128-bit block size,
 * but a 64-bit version (needed for Speck64) is fairly straightforward; the math
 * is just done in GF(2^64) instead of GF(2^128), with the reducing polynomial
 * x^64 + x^4 + x^3 + x + 1 from the original XEX paper (Rogaway, 2004:
 * "Efficient Instantiations of Tweakable Blockciphers and Refinements to Modes
 * OCB and PMAC"), represented as 0x1B.
 */

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>
#include <crypto/algapi.h>
#include <crypto/gf128mul.h>
#include <crypto/speck.h>
#include <crypto/xts.h>
#include <linux/kernel.h>
#include <linux/module.h>

/* The assembly functions only handle multiples of 128 bytes */
#define SPECK_NEON_CHUNK_SIZE	128

/* Speck128 */

struct speck128_xts_tfm_ctx {
	struct speck128_tfm_ctx main_key;
	struct speck128_tfm_ctx tweak_key;
};

asmlinkage void speck128_xts_encrypt_neon(const u64 *round_keys, int nrounds,
					  void *dst, const void *src,
					  unsigned int nbytes, void *tweak);

asmlinkage void speck128_xts_decrypt_neon(const u64 *round_keys, int nrounds,
					  void *dst, const void *src,
					  unsigned int nbytes, void *tweak);

typedef void (*speck128_crypt_one_t)(const struct speck128_tfm_ctx *,
				     u8 *, const u8 *);
typedef void (*speck128_xts_crypt_many_t)(const u64 *, int, void *,
					  const void *, unsigned int, void *);

static __always_inline int
__speck128_xts_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		     struct scatterlist *src, unsigned int nbytes,
		     speck128_crypt_one_t crypt_one,
		     speck128_xts_crypt_many_t crypt_many)
{
	struct crypto_blkcipher *tfm = desc->tfm;
	const struct speck128_xts_tfm_ctx *ctx = crypto_blkcipher_ctx(tfm);
	struct blkcipher_walk walk;
	le128 tweak;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt_block(desc, &walk, SPECK_NEON_CHUNK_SIZE);

	crypto_speck128_encrypt(&ctx->tweak_key, (u8 *)&tweak, walk.iv);

	while (walk.nbytes > 0) {
		unsigned int nbytes = walk.nbytes;
		u8 *dst = walk.dst.virt.addr;
		const u8 *src = walk.src.virt.addr;

		if (nbytes >= SPECK_NEON_CHUNK_SIZE && may_use_simd()) {
			unsigned int count;

			count = round_down(nbytes, SPECK_NEON_CHUNK_SIZE);
			kernel_neon_begin();
			(*crypt_many)(ctx->main_key.round_keys,
				      ctx->main_key.nrounds,
				      dst, src, count, &tweak);
			kernel_neon_end();
			dst += count;
			src += count;
			nbytes -= count;
		}

		/* Handle any remainder with generic code */
		while (nbytes >= sizeof(tweak)) {
			le128_xor((le128 *)dst, (const le128 *)src, &tweak);
			(*crypt_one)(&ctx->main_key, dst, dst);
			le128_xor((le128 *)dst, (const le128 *)dst, &tweak);
			gf128mul_x_ble((be128 *)&tweak, (const be128 *)&tweak);

			dst += sizeof(tweak);
			src += sizeof(tweak);
			nbytes -= sizeof(tweak);
		}
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	return err;
}

static int speck128_xts_encrypt(struct blkcipher_desc *desc,
				struct scatterlist *dst,
				struct scatterlist *src,
				unsigned int nbytes)
{
	return __speck128_xts_crypt(desc, dst, src, nbytes,
				    crypto_speck128_encrypt,
				    speck128_xts_encrypt_neon);
}

static int speck128_xts_decrypt(struct blkcipher_desc *desc,
				struct scatterlist *dst,
				struct scatterlist *src,
				unsigned int nbytes)
{
	return __speck128_xts_crypt(desc, dst, src, nbytes,
				    crypto_speck128_decrypt,
				    speck128_xts_decrypt_neon);
}

static int speck128_xts_setkey(struct crypto_tfm *tfm, const u8 *key,
			       unsigned int keylen)
{
	struct speck128_xts_tfm_ctx *ctx = crypto_tfm_ctx(tfm);
	int err;

	if (keylen % 2)
		return -EINVAL;

	keylen /= 2;

	err = crypto_speck128_setkey(&ctx->main_key, key, keylen);
	if (err)
		return err;

	return crypto_speck128_setkey(&ctx->tweak_key, key + keylen, keylen);
}

/* Speck64 */

struct speck64_xts_tfm_ctx {
	struct speck64_tfm_ctx main_key;
	struct speck64_tfm_ctx tweak_key;
};

asmlinkage void speck64_xts_encrypt_neon(const u32 *round_keys, int nrounds,
					 void *dst, const void *src,
					 unsigned int nbytes, void *tweak);

asmlinkage void speck64_xts_decrypt_neon(const u32 *round_keys, int nrounds,
					 void *dst, const void *src,
					 unsigned int nbytes, void *tweak);

typedef void (*speck64_crypt_one_t)(const struct speck64_tfm_ctx *,
				    u8 *, const u8 *);
typedef void (*speck64_xts_crypt_many_t)(const u32 *, int, void *,
					 const void *, unsigned int, void *);

static __always_inline int
__speck64_xts_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		    struct scatterlist *src, unsigned int nbytes,
		    speck64_crypt_one_t crypt_one,
		    speck64_xts_crypt_many_t crypt_many)
{
	struct crypto_blkcipher *tfm = desc->tfm;
	const struct speck64_xts_tfm_ctx *ctx = crypto_blkcipher_ctx(tfm);
	struct blkcipher_walk walk;
	__le64 tweak;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt_block(desc, &walk, SPECK_NEON_CHUNK_SIZE);

	crypto_speck64_encrypt(&ctx->tweak_key, (u8 *)&tweak, walk.iv);

	while (walk.nbytes > 0) {
		unsigned int nbytes = walk.nbytes;
		u8 *dst = walk.dst.virt.addr;
		const u8 *src = walk.src.virt.addr;

		if (nbytes >= SPECK_NEON_CHUNK_SIZE && may_use_simd()) {
			unsigned int count;

			count = round_down(nbytes, SPECK_NEON_CHUNK_SIZE);
			kernel_neon_begin();
			(*crypt_many)(ctx->main_key.round_keys,
				      ctx->main_key.nrounds,
				      dst, src, count, &tweak);
			kernel_neon_end();
			dst += count;
			src += count;
			nbytes -= count;
		}

		/* Handle any remainder with generic code */
		while (nbytes >= sizeof(tweak)) {
			*(__le64 *)dst = *(__le64 *)src ^ tweak;
			(*crypt_one)(&ctx->main_key, dst, dst);
			*(__le64 *)dst ^= tweak;
			tweak = cpu_to_le64((le64_to_cpu(tweak) << 1) ^
					    ((tweak & cpu_to_le64(1ULL << 63)) ?
					     0x1B : 0));
			dst += sizeof(tweak);
			src += sizeof(tweak);
			nbytes -= sizeof(tweak);
		}
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	return err;
}

static int speck64_xts_encrypt(struct blkcipher_desc *desc,
			       struct scatterlist *dst, struct scatterlist *src,
			       unsigned int nbytes)
{
	return __speck64_xts_crypt(desc, dst, src, nbytes,
				   crypto_speck64_encrypt,
				   speck64_xts_encrypt_neon);
}

static int speck64_xts_decrypt(struct blkcipher_desc *desc,
			       struct scatterlist *dst, struct scatterlist *src,
			       unsigned int nbytes)
{
	return __speck64_xts_crypt(desc, dst, src, nbytes,
				   crypto_speck64_decrypt,
				   speck64_xts_decrypt_neon);
}

static int speck64_xts_setkey(struct crypto_tfm *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct speck64_xts_tfm_ctx *ctx = crypto_tfm_ctx(tfm);
	int err;

	if (keylen % 2)
		return -EINVAL;

	keylen /= 2;

	err = crypto_speck64_setkey(&ctx->main_key, key, keylen);
	if (err)
		return err;

	return crypto_speck64_setkey(&ctx->tweak_key, key + keylen, keylen);
}

static struct crypto_alg speck_algs[] = {
	{
		.cra_name		= "xts(speck128)",
		.cra_driver_name	= "xts-speck128-neon",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
		.cra_blocksize		= SPECK128_BLOCK_SIZE,
		.cra_type		= &crypto_blkcipher_type,
		.cra_ctxsize		= sizeof(struct speck128_xts_tfm_ctx),
		.cra_alignmask		= 7,
		.cra_module		= THIS_MODULE,
		.cra_u = {
			.blkcipher = {
				.min_keysize		= 2 * SPECK128_128_KEY_SIZE,
				.max_keysize		= 2 * SPECK128_256_KEY_SIZE,
				.ivsize			= SPECK128_BLOCK_SIZE,
				.setkey			= speck128_xts_setkey,
				.encrypt		= speck128_xts_encrypt,
				.decrypt		= speck128_xts_decrypt,
			}
		}
	}, {
		.cra_name		= "xts(speck64)",
		.cra_driver_name	= "xts-speck64-neon",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
		.cra_blocksize		= SPECK64_BLOCK_SIZE,
		.cra_type		= &crypto_blkcipher_type,
		.cra_ctxsize		= sizeof(struct speck64_xts_tfm_ctx),
		.cra_alignmask		= 7,
		.cra_module		= THIS_MODULE,
		.cra_u = {
			.blkcipher = {
				.min_keysize		= 2 * SPECK64_96_KEY_SIZE,
				.max_keysize		= 2 * SPECK64_128_KEY_SIZE,
				.ivsize			= SPECK64_BLOCK_SIZE,
				.setkey			= speck64_xts_setkey,
				.encrypt		= speck64_xts_encrypt,
				.decrypt		= speck64_xts_decrypt,
			}
		}
	}
};

static int __init speck_neon_module_init(void)
{
	if (!(elf_hwcap & HWCAP_NEON))
		return -ENODEV;
	return crypto_register_algs(speck_algs, ARRAY_SIZE(speck_algs));
}

static void __exit speck_neon_module_exit(void)
{
	crypto_unregister_algs(speck_algs, ARRAY_SIZE(speck_algs));
}

module_init(speck_neon_module_init);
module_exit(speck_neon_module_exit);

MODULE_DESCRIPTION("Speck block cipher (NEON-accelerated)");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eric Biggers <ebiggers@google.com>");
MODULE_ALIAS_CRYPTO("xts(speck128)");
MODULE_ALIAS_CRYPTO("xts-speck128-neon");
MODULE_ALIAS_CRYPTO("xts(speck64)");
MODULE_ALIAS_CRYPTO("xts-speck64-neon");
