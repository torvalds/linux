// SPDX-License-Identifier: (GPL-2.0-only OR Apache-2.0)
/*
 * Generic implementation of the BLAKE2b digest algorithm.  Based on the BLAKE2b
 * reference implementation, but it has been heavily modified for use in the
 * kernel.  The reference implementation was:
 *
 *	Copyright 2012, Samuel Neves <sneves@dei.uc.pt>.  You may use this under
 *	the terms of the CC0, the OpenSSL Licence, or the Apache Public License
 *	2.0, at your option.  The terms of these licenses can be found at:
 *
 *	- CC0 1.0 Universal : http://creativecommons.org/publicdomain/zero/1.0
 *	- OpenSSL license   : https://www.openssl.org/source/license.html
 *	- Apache 2.0        : https://www.apache.org/licenses/LICENSE-2.0
 *
 * More information about BLAKE2 can be found at https://blake2.net.
 */

#include <crypto/internal/blake2b.h>
#include <crypto/internal/hash.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>

static const u8 blake2b_sigma[12][16] = {
	{  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
	{ 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 },
	{ 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 },
	{  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 },
	{  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 },
	{  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 },
	{ 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 },
	{ 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 },
	{  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 },
	{ 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13,  0 },
	{  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
	{ 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 }
};

static void blake2b_increment_counter(struct blake2b_state *S, const u64 inc)
{
	S->t[0] += inc;
	S->t[1] += (S->t[0] < inc);
}

#define G(r,i,a,b,c,d)                                  \
	do {                                            \
		a = a + b + m[blake2b_sigma[r][2*i+0]]; \
		d = ror64(d ^ a, 32);                   \
		c = c + d;                              \
		b = ror64(b ^ c, 24);                   \
		a = a + b + m[blake2b_sigma[r][2*i+1]]; \
		d = ror64(d ^ a, 16);                   \
		c = c + d;                              \
		b = ror64(b ^ c, 63);                   \
	} while (0)

#define ROUND(r)                                \
	do {                                    \
		G(r,0,v[ 0],v[ 4],v[ 8],v[12]); \
		G(r,1,v[ 1],v[ 5],v[ 9],v[13]); \
		G(r,2,v[ 2],v[ 6],v[10],v[14]); \
		G(r,3,v[ 3],v[ 7],v[11],v[15]); \
		G(r,4,v[ 0],v[ 5],v[10],v[15]); \
		G(r,5,v[ 1],v[ 6],v[11],v[12]); \
		G(r,6,v[ 2],v[ 7],v[ 8],v[13]); \
		G(r,7,v[ 3],v[ 4],v[ 9],v[14]); \
	} while (0)

static void blake2b_compress_one_generic(struct blake2b_state *S,
					 const u8 block[BLAKE2B_BLOCK_SIZE])
{
	u64 m[16];
	u64 v[16];
	size_t i;

	for (i = 0; i < 16; ++i)
		m[i] = get_unaligned_le64(block + i * sizeof(m[i]));

	for (i = 0; i < 8; ++i)
		v[i] = S->h[i];

	v[ 8] = BLAKE2B_IV0;
	v[ 9] = BLAKE2B_IV1;
	v[10] = BLAKE2B_IV2;
	v[11] = BLAKE2B_IV3;
	v[12] = BLAKE2B_IV4 ^ S->t[0];
	v[13] = BLAKE2B_IV5 ^ S->t[1];
	v[14] = BLAKE2B_IV6 ^ S->f[0];
	v[15] = BLAKE2B_IV7 ^ S->f[1];

	ROUND(0);
	ROUND(1);
	ROUND(2);
	ROUND(3);
	ROUND(4);
	ROUND(5);
	ROUND(6);
	ROUND(7);
	ROUND(8);
	ROUND(9);
	ROUND(10);
	ROUND(11);
#ifdef CONFIG_CC_IS_CLANG
#pragma nounroll /* https://llvm.org/pr45803 */
#endif
	for (i = 0; i < 8; ++i)
		S->h[i] = S->h[i] ^ v[i] ^ v[i + 8];
}

#undef G
#undef ROUND

static void blake2b_compress_generic(struct blake2b_state *state,
				     const u8 *block, size_t nblocks, u32 inc)
{
	do {
		blake2b_increment_counter(state, inc);
		blake2b_compress_one_generic(state, block);
		block += BLAKE2B_BLOCK_SIZE;
	} while (--nblocks);
}

static int crypto_blake2b_update_generic(struct shash_desc *desc,
					 const u8 *in, unsigned int inlen)
{
	return crypto_blake2b_update_bo(desc, in, inlen,
					blake2b_compress_generic);
}

static int crypto_blake2b_finup_generic(struct shash_desc *desc, const u8 *in,
					unsigned int inlen, u8 *out)
{
	return crypto_blake2b_finup(desc, in, inlen, out,
				    blake2b_compress_generic);
}

#define BLAKE2B_ALG(name, driver_name, digest_size)			\
	{								\
		.base.cra_name		= name,				\
		.base.cra_driver_name	= driver_name,			\
		.base.cra_priority	= 100,				\
		.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY |	\
					  CRYPTO_AHASH_ALG_BLOCK_ONLY |	\
					  CRYPTO_AHASH_ALG_FINAL_NONZERO, \
		.base.cra_blocksize	= BLAKE2B_BLOCK_SIZE,		\
		.base.cra_ctxsize	= sizeof(struct blake2b_tfm_ctx), \
		.base.cra_module	= THIS_MODULE,			\
		.digestsize		= digest_size,			\
		.setkey			= crypto_blake2b_setkey,	\
		.init			= crypto_blake2b_init,		\
		.update			= crypto_blake2b_update_generic, \
		.finup			= crypto_blake2b_finup_generic,	\
		.descsize		= BLAKE2B_DESC_SIZE,		\
		.statesize		= BLAKE2B_STATE_SIZE,		\
	}

static struct shash_alg blake2b_algs[] = {
	BLAKE2B_ALG("blake2b-160", "blake2b-160-generic",
		    BLAKE2B_160_HASH_SIZE),
	BLAKE2B_ALG("blake2b-256", "blake2b-256-generic",
		    BLAKE2B_256_HASH_SIZE),
	BLAKE2B_ALG("blake2b-384", "blake2b-384-generic",
		    BLAKE2B_384_HASH_SIZE),
	BLAKE2B_ALG("blake2b-512", "blake2b-512-generic",
		    BLAKE2B_512_HASH_SIZE),
};

static int __init blake2b_mod_init(void)
{
	return crypto_register_shashes(blake2b_algs, ARRAY_SIZE(blake2b_algs));
}

static void __exit blake2b_mod_fini(void)
{
	crypto_unregister_shashes(blake2b_algs, ARRAY_SIZE(blake2b_algs));
}

module_init(blake2b_mod_init);
module_exit(blake2b_mod_fini);

MODULE_AUTHOR("David Sterba <kdave@kernel.org>");
MODULE_DESCRIPTION("BLAKE2b generic implementation");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("blake2b-160");
MODULE_ALIAS_CRYPTO("blake2b-160-generic");
MODULE_ALIAS_CRYPTO("blake2b-256");
MODULE_ALIAS_CRYPTO("blake2b-256-generic");
MODULE_ALIAS_CRYPTO("blake2b-384");
MODULE_ALIAS_CRYPTO("blake2b-384-generic");
MODULE_ALIAS_CRYPTO("blake2b-512");
MODULE_ALIAS_CRYPTO("blake2b-512-generic");
