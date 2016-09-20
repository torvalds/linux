/*
 * Cryptographic API.
 *
 * SHA-3, as specified in
 * http://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.202.pdf
 *
 * SHA-3 code by Jeff Garzik <jeff@garzik.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)•
 * any later version.
 *
 */
#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <crypto/sha3.h>
#include <asm/byteorder.h>

#define KECCAK_ROUNDS 24

#define ROTL64(x, y) (((x) << (y)) | ((x) >> (64 - (y))))

static const u64 keccakf_rndc[24] = {
	0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
	0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
	0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
	0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
	0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
	0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
	0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
	0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
};

static const int keccakf_rotc[24] = {
	1,  3,  6,  10, 15, 21, 28, 36, 45, 55, 2,  14,
	27, 41, 56, 8,  25, 43, 62, 18, 39, 61, 20, 44
};

static const int keccakf_piln[24] = {
	10, 7,  11, 17, 18, 3, 5,  16, 8,  21, 24, 4,
	15, 23, 19, 13, 12, 2, 20, 14, 22, 9,  6,  1
};

/* update the state with given number of rounds */

static void keccakf(u64 st[25])
{
	int i, j, round;
	u64 t, bc[5];

	for (round = 0; round < KECCAK_ROUNDS; round++) {

		/* Theta */
		for (i = 0; i < 5; i++)
			bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15]
				^ st[i + 20];

		for (i = 0; i < 5; i++) {
			t = bc[(i + 4) % 5] ^ ROTL64(bc[(i + 1) % 5], 1);
			for (j = 0; j < 25; j += 5)
				st[j + i] ^= t;
		}

		/* Rho Pi */
		t = st[1];
		for (i = 0; i < 24; i++) {
			j = keccakf_piln[i];
			bc[0] = st[j];
			st[j] = ROTL64(t, keccakf_rotc[i]);
			t = bc[0];
		}

		/* Chi */
		for (j = 0; j < 25; j += 5) {
			for (i = 0; i < 5; i++)
				bc[i] = st[j + i];
			for (i = 0; i < 5; i++)
				st[j + i] ^= (~bc[(i + 1) % 5]) &
					     bc[(i + 2) % 5];
		}

		/* Iota */
		st[0] ^= keccakf_rndc[round];
	}
}

static void sha3_init(struct sha3_state *sctx, unsigned int digest_sz)
{
	memset(sctx, 0, sizeof(*sctx));
	sctx->md_len = digest_sz;
	sctx->rsiz = 200 - 2 * digest_sz;
	sctx->rsizw = sctx->rsiz / 8;
}

static int sha3_224_init(struct shash_desc *desc)
{
	struct sha3_state *sctx = shash_desc_ctx(desc);

	sha3_init(sctx, SHA3_224_DIGEST_SIZE);
	return 0;
}

static int sha3_256_init(struct shash_desc *desc)
{
	struct sha3_state *sctx = shash_desc_ctx(desc);

	sha3_init(sctx, SHA3_256_DIGEST_SIZE);
	return 0;
}

static int sha3_384_init(struct shash_desc *desc)
{
	struct sha3_state *sctx = shash_desc_ctx(desc);

	sha3_init(sctx, SHA3_384_DIGEST_SIZE);
	return 0;
}

static int sha3_512_init(struct shash_desc *desc)
{
	struct sha3_state *sctx = shash_desc_ctx(desc);

	sha3_init(sctx, SHA3_512_DIGEST_SIZE);
	return 0;
}

static int sha3_update(struct shash_desc *desc, const u8 *data,
		       unsigned int len)
{
	struct sha3_state *sctx = shash_desc_ctx(desc);
	unsigned int done;
	const u8 *src;

	done = 0;
	src = data;

	if ((sctx->partial + len) > (sctx->rsiz - 1)) {
		if (sctx->partial) {
			done = -sctx->partial;
			memcpy(sctx->buf + sctx->partial, data,
			       done + sctx->rsiz);
			src = sctx->buf;
		}

		do {
			unsigned int i;

			for (i = 0; i < sctx->rsizw; i++)
				sctx->st[i] ^= ((u64 *) src)[i];
			keccakf(sctx->st);

			done += sctx->rsiz;
			src = data + done;
		} while (done + (sctx->rsiz - 1) < len);

		sctx->partial = 0;
	}
	memcpy(sctx->buf + sctx->partial, src, len - done);
	sctx->partial += (len - done);

	return 0;
}

static int sha3_final(struct shash_desc *desc, u8 *out)
{
	struct sha3_state *sctx = shash_desc_ctx(desc);
	unsigned int i, inlen = sctx->partial;

	sctx->buf[inlen++] = 0x06;
	memset(sctx->buf + inlen, 0, sctx->rsiz - inlen);
	sctx->buf[sctx->rsiz - 1] |= 0x80;

	for (i = 0; i < sctx->rsizw; i++)
		sctx->st[i] ^= ((u64 *) sctx->buf)[i];

	keccakf(sctx->st);

	for (i = 0; i < sctx->rsizw; i++)
		sctx->st[i] = cpu_to_le64(sctx->st[i]);

	memcpy(out, sctx->st, sctx->md_len);

	memset(sctx, 0, sizeof(*sctx));
	return 0;
}

static struct shash_alg sha3_224 = {
	.digestsize	=	SHA3_224_DIGEST_SIZE,
	.init		=	sha3_224_init,
	.update		=	sha3_update,
	.final		=	sha3_final,
	.descsize	=	sizeof(struct sha3_state),
	.base		=	{
		.cra_name	=	"sha3-224",
		.cra_driver_name =	"sha3-224-generic",
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	SHA3_224_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static struct shash_alg sha3_256 = {
	.digestsize	=	SHA3_256_DIGEST_SIZE,
	.init		=	sha3_256_init,
	.update		=	sha3_update,
	.final		=	sha3_final,
	.descsize	=	sizeof(struct sha3_state),
	.base		=	{
		.cra_name	=	"sha3-256",
		.cra_driver_name =	"sha3-256-generic",
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	SHA3_256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static struct shash_alg sha3_384 = {
	.digestsize	=	SHA3_384_DIGEST_SIZE,
	.init		=	sha3_384_init,
	.update		=	sha3_update,
	.final		=	sha3_final,
	.descsize	=	sizeof(struct sha3_state),
	.base		=	{
		.cra_name	=	"sha3-384",
		.cra_driver_name =	"sha3-384-generic",
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	SHA3_384_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static struct shash_alg sha3_512 = {
	.digestsize	=	SHA3_512_DIGEST_SIZE,
	.init		=	sha3_512_init,
	.update		=	sha3_update,
	.final		=	sha3_final,
	.descsize	=	sizeof(struct sha3_state),
	.base		=	{
		.cra_name	=	"sha3-512",
		.cra_driver_name =	"sha3-512-generic",
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	SHA3_512_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int __init sha3_generic_mod_init(void)
{
	int ret;

	ret = crypto_register_shash(&sha3_224);
	if (ret < 0)
		goto err_out;
	ret = crypto_register_shash(&sha3_256);
	if (ret < 0)
		goto err_out_224;
	ret = crypto_register_shash(&sha3_384);
	if (ret < 0)
		goto err_out_256;
	ret = crypto_register_shash(&sha3_512);
	if (ret < 0)
		goto err_out_384;

	return 0;

err_out_384:
	crypto_unregister_shash(&sha3_384);
err_out_256:
	crypto_unregister_shash(&sha3_256);
err_out_224:
	crypto_unregister_shash(&sha3_224);
err_out:
	return ret;
}

static void __exit sha3_generic_mod_fini(void)
{
	crypto_unregister_shash(&sha3_224);
	crypto_unregister_shash(&sha3_256);
	crypto_unregister_shash(&sha3_384);
	crypto_unregister_shash(&sha3_512);
}

module_init(sha3_generic_mod_init);
module_exit(sha3_generic_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA-3 Secure Hash Algorithm");

MODULE_ALIAS_CRYPTO("sha3-224");
MODULE_ALIAS_CRYPTO("sha3-224-generic");
MODULE_ALIAS_CRYPTO("sha3-256");
MODULE_ALIAS_CRYPTO("sha3-256-generic");
MODULE_ALIAS_CRYPTO("sha3-384");
MODULE_ALIAS_CRYPTO("sha3-384-generic");
MODULE_ALIAS_CRYPTO("sha3-512");
MODULE_ALIAS_CRYPTO("sha3-512-generic");
