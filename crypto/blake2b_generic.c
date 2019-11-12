// SPDX-License-Identifier: (GPL-2.0-only OR Apache-2.0)
/*
 * BLAKE2b reference source code package - reference C implementations
 *
 * Copyright 2012, Samuel Neves <sneves@dei.uc.pt>.  You may use this under the
 * terms of the CC0, the OpenSSL Licence, or the Apache Public License 2.0, at
 * your option.  The terms of these licenses can be found at:
 *
 * - CC0 1.0 Universal : http://creativecommons.org/publicdomain/zero/1.0
 * - OpenSSL license   : https://www.openssl.org/source/license.html
 * - Apache 2.0        : http://www.apache.org/licenses/LICENSE-2.0
 *
 * More information about the BLAKE2 hash function can be found at
 * https://blake2.net.
 *
 * Note: the original sources have been modified for inclusion in linux kernel
 * in terms of coding style, using generic helpers and simplifications of error
 * handling.
 */

#include <asm/unaligned.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <crypto/internal/hash.h>

#define BLAKE2B_160_DIGEST_SIZE		(160 / 8)
#define BLAKE2B_256_DIGEST_SIZE		(256 / 8)
#define BLAKE2B_384_DIGEST_SIZE		(384 / 8)
#define BLAKE2B_512_DIGEST_SIZE		(512 / 8)

enum blake2b_constant {
	BLAKE2B_BLOCKBYTES    = 128,
	BLAKE2B_OUTBYTES      = 64,
	BLAKE2B_KEYBYTES      = 64,
	BLAKE2B_SALTBYTES     = 16,
	BLAKE2B_PERSONALBYTES = 16
};

struct blake2b_state {
	u64      h[8];
	u64      t[2];
	u64      f[2];
	u8       buf[BLAKE2B_BLOCKBYTES];
	size_t   buflen;
	size_t   outlen;
	u8       last_node;
};

struct blake2b_param {
	u8 digest_length;			/* 1 */
	u8 key_length;				/* 2 */
	u8 fanout;				/* 3 */
	u8 depth;				/* 4 */
	__le32 leaf_length;			/* 8 */
	__le32 node_offset;			/* 12 */
	__le32 xof_length;			/* 16 */
	u8 node_depth;				/* 17 */
	u8 inner_length;			/* 18 */
	u8 reserved[14];			/* 32 */
	u8 salt[BLAKE2B_SALTBYTES];		/* 48 */
	u8 personal[BLAKE2B_PERSONALBYTES];	/* 64 */
} __packed;

static const u64 blake2b_IV[8] = {
	0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
	0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
	0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
	0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

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

static void blake2b_update(struct blake2b_state *S, const void *pin, size_t inlen);

static void blake2b_set_lastnode(struct blake2b_state *S)
{
	S->f[1] = (u64)-1;
}

static void blake2b_set_lastblock(struct blake2b_state *S)
{
	if (S->last_node)
		blake2b_set_lastnode(S);

	S->f[0] = (u64)-1;
}

static void blake2b_increment_counter(struct blake2b_state *S, const u64 inc)
{
	S->t[0] += inc;
	S->t[1] += (S->t[0] < inc);
}

static void blake2b_init0(struct blake2b_state *S)
{
	size_t i;

	memset(S, 0, sizeof(struct blake2b_state));

	for (i = 0; i < 8; ++i)
		S->h[i] = blake2b_IV[i];
}

/* init xors IV with input parameter block */
static void blake2b_init_param(struct blake2b_state *S,
			       const struct blake2b_param *P)
{
	const u8 *p = (const u8 *)(P);
	size_t i;

	blake2b_init0(S);

	/* IV XOR ParamBlock */
	for (i = 0; i < 8; ++i)
		S->h[i] ^= get_unaligned_le64(p + sizeof(S->h[i]) * i);

	S->outlen = P->digest_length;
}

static void blake2b_init(struct blake2b_state *S, size_t outlen)
{
	struct blake2b_param P;

	P.digest_length = (u8)outlen;
	P.key_length    = 0;
	P.fanout        = 1;
	P.depth         = 1;
	P.leaf_length   = 0;
	P.node_offset   = 0;
	P.xof_length    = 0;
	P.node_depth    = 0;
	P.inner_length  = 0;
	memset(P.reserved, 0, sizeof(P.reserved));
	memset(P.salt,     0, sizeof(P.salt));
	memset(P.personal, 0, sizeof(P.personal));
	blake2b_init_param(S, &P);
}

static void blake2b_init_key(struct blake2b_state *S, size_t outlen,
			     const void *key, size_t keylen)
{
	struct blake2b_param P;

	P.digest_length = (u8)outlen;
	P.key_length    = (u8)keylen;
	P.fanout        = 1;
	P.depth         = 1;
	P.leaf_length   = 0;
	P.node_offset   = 0;
	P.xof_length    = 0;
	P.node_depth    = 0;
	P.inner_length  = 0;
	memset(P.reserved, 0, sizeof(P.reserved));
	memset(P.salt,     0, sizeof(P.salt));
	memset(P.personal, 0, sizeof(P.personal));

	blake2b_init_param(S, &P);

	{
		u8 block[BLAKE2B_BLOCKBYTES];

		memset(block, 0, BLAKE2B_BLOCKBYTES);
		memcpy(block, key, keylen);
		blake2b_update(S, block, BLAKE2B_BLOCKBYTES);
		memzero_explicit(block, BLAKE2B_BLOCKBYTES);
	}
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

static void blake2b_compress(struct blake2b_state *S,
			     const u8 block[BLAKE2B_BLOCKBYTES])
{
	u64 m[16];
	u64 v[16];
	size_t i;

	for (i = 0; i < 16; ++i)
		m[i] = get_unaligned_le64(block + i * sizeof(m[i]));

	for (i = 0; i < 8; ++i)
		v[i] = S->h[i];

	v[ 8] = blake2b_IV[0];
	v[ 9] = blake2b_IV[1];
	v[10] = blake2b_IV[2];
	v[11] = blake2b_IV[3];
	v[12] = blake2b_IV[4] ^ S->t[0];
	v[13] = blake2b_IV[5] ^ S->t[1];
	v[14] = blake2b_IV[6] ^ S->f[0];
	v[15] = blake2b_IV[7] ^ S->f[1];

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

	for (i = 0; i < 8; ++i)
		S->h[i] = S->h[i] ^ v[i] ^ v[i + 8];
}

#undef G
#undef ROUND

static void blake2b_update(struct blake2b_state *S, const void *pin, size_t inlen)
{
	const u8 *in = (const u8 *)pin;

	if (inlen > 0) {
		size_t left = S->buflen;
		size_t fill = BLAKE2B_BLOCKBYTES - left;

		if (inlen > fill) {
			S->buflen = 0;
			/* Fill buffer */
			memcpy(S->buf + left, in, fill);
			blake2b_increment_counter(S, BLAKE2B_BLOCKBYTES);
			/* Compress */
			blake2b_compress(S, S->buf);
			in += fill;
			inlen -= fill;
			while (inlen > BLAKE2B_BLOCKBYTES) {
				blake2b_increment_counter(S, BLAKE2B_BLOCKBYTES);
				blake2b_compress(S, in);
				in += BLAKE2B_BLOCKBYTES;
				inlen -= BLAKE2B_BLOCKBYTES;
			}
		}
		memcpy(S->buf + S->buflen, in, inlen);
		S->buflen += inlen;
	}
}

struct digest_tfm_ctx {
	u8 key[BLAKE2B_KEYBYTES];
	unsigned int keylen;
};

static int digest_setkey(struct crypto_shash *tfm, const u8 *key,
			 unsigned int keylen)
{
	struct digest_tfm_ctx *mctx = crypto_shash_ctx(tfm);

	if (keylen == 0 || keylen > BLAKE2B_KEYBYTES) {
		crypto_shash_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memcpy(mctx->key, key, keylen);
	mctx->keylen = keylen;

	return 0;
}

static int digest_init(struct shash_desc *desc)
{
	struct digest_tfm_ctx *mctx = crypto_shash_ctx(desc->tfm);
	struct blake2b_state *state = shash_desc_ctx(desc);
	const int digestsize = crypto_shash_digestsize(desc->tfm);

	if (mctx->keylen == 0)
		blake2b_init(state, digestsize);
	else
		blake2b_init_key(state, digestsize, mctx->key, mctx->keylen);
	return 0;
}

static int digest_update(struct shash_desc *desc, const u8 *data,
			 unsigned int length)
{
	struct blake2b_state *state = shash_desc_ctx(desc);

	blake2b_update(state, data, length);
	return 0;
}

static int blake2b_final(struct shash_desc *desc, u8 *out)
{
	struct blake2b_state *state = shash_desc_ctx(desc);
	const int digestsize = crypto_shash_digestsize(desc->tfm);
	size_t i;

	blake2b_increment_counter(state, state->buflen);
	blake2b_set_lastblock(state);
	/* Padding */
	memset(state->buf + state->buflen, 0, BLAKE2B_BLOCKBYTES - state->buflen);
	blake2b_compress(state, state->buf);

	/* Avoid temporary buffer and switch the internal output to LE order */
	for (i = 0; i < ARRAY_SIZE(state->h); i++)
		__cpu_to_le64s(&state->h[i]);

	memcpy(out, state->h, digestsize);
	return 0;
}

static struct shash_alg blake2b_algs[] = {
	{
		.base.cra_name		= "blake2b-160",
		.base.cra_driver_name	= "blake2b-160-generic",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY,
		.base.cra_blocksize	= BLAKE2B_BLOCKBYTES,
		.base.cra_ctxsize	= sizeof(struct digest_tfm_ctx),
		.base.cra_module	= THIS_MODULE,
		.digestsize		= BLAKE2B_160_DIGEST_SIZE,
		.setkey			= digest_setkey,
		.init			= digest_init,
		.update			= digest_update,
		.final			= blake2b_final,
		.descsize		= sizeof(struct blake2b_state),
	}, {
		.base.cra_name		= "blake2b-256",
		.base.cra_driver_name	= "blake2b-256-generic",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY,
		.base.cra_blocksize	= BLAKE2B_BLOCKBYTES,
		.base.cra_ctxsize	= sizeof(struct digest_tfm_ctx),
		.base.cra_module	= THIS_MODULE,
		.digestsize		= BLAKE2B_256_DIGEST_SIZE,
		.setkey			= digest_setkey,
		.init			= digest_init,
		.update			= digest_update,
		.final			= blake2b_final,
		.descsize		= sizeof(struct blake2b_state),
	}, {
		.base.cra_name		= "blake2b-384",
		.base.cra_driver_name	= "blake2b-384-generic",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY,
		.base.cra_blocksize	= BLAKE2B_BLOCKBYTES,
		.base.cra_ctxsize	= sizeof(struct digest_tfm_ctx),
		.base.cra_module	= THIS_MODULE,
		.digestsize		= BLAKE2B_384_DIGEST_SIZE,
		.setkey			= digest_setkey,
		.init			= digest_init,
		.update			= digest_update,
		.final			= blake2b_final,
		.descsize		= sizeof(struct blake2b_state),
	}, {
		.base.cra_name		= "blake2b-512",
		.base.cra_driver_name	= "blake2b-512-generic",
		.base.cra_priority	= 100,
		.base.cra_flags		= CRYPTO_ALG_OPTIONAL_KEY,
		.base.cra_blocksize	= BLAKE2B_BLOCKBYTES,
		.base.cra_ctxsize	= sizeof(struct digest_tfm_ctx),
		.base.cra_module	= THIS_MODULE,
		.digestsize		= BLAKE2B_512_DIGEST_SIZE,
		.setkey			= digest_setkey,
		.init			= digest_init,
		.update			= digest_update,
		.final			= blake2b_final,
		.descsize		= sizeof(struct blake2b_state),
	}
};

static int __init blake2b_mod_init(void)
{
	BUILD_BUG_ON(sizeof(struct blake2b_param) != BLAKE2B_OUTBYTES);

	return crypto_register_shashes(blake2b_algs, ARRAY_SIZE(blake2b_algs));
}

static void __exit blake2b_mod_fini(void)
{
	crypto_unregister_shashes(blake2b_algs, ARRAY_SIZE(blake2b_algs));
}

subsys_initcall(blake2b_mod_init);
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
