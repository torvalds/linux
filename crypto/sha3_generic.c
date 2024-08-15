// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API.
 *
 * SHA-3, as specified in
 * https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.202.pdf
 *
 * SHA-3 code by Jeff Garzik <jeff@garzik.org>
 *               Ard Biesheuvel <ard.biesheuvel@linaro.org>
 */
#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <crypto/sha3.h>
#include <asm/unaligned.h>

/*
 * On some 32-bit architectures (h8300), GCC ends up using
 * over 1 KB of stack if we inline the round calculation into the loop
 * in keccakf(). On the other hand, on 64-bit architectures with plenty
 * of [64-bit wide] general purpose registers, not inlining it severely
 * hurts performance. So let's use 64-bitness as a heuristic to decide
 * whether to inline or not.
 */
#ifdef CONFIG_64BIT
#define SHA3_INLINE	inline
#else
#define SHA3_INLINE	noinline
#endif

#define KECCAK_ROUNDS 24

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

/* update the state with given number of rounds */

static SHA3_INLINE void keccakf_round(u64 st[25])
{
	u64 t[5], tt, bc[5];

	/* Theta */
	bc[0] = st[0] ^ st[5] ^ st[10] ^ st[15] ^ st[20];
	bc[1] = st[1] ^ st[6] ^ st[11] ^ st[16] ^ st[21];
	bc[2] = st[2] ^ st[7] ^ st[12] ^ st[17] ^ st[22];
	bc[3] = st[3] ^ st[8] ^ st[13] ^ st[18] ^ st[23];
	bc[4] = st[4] ^ st[9] ^ st[14] ^ st[19] ^ st[24];

	t[0] = bc[4] ^ rol64(bc[1], 1);
	t[1] = bc[0] ^ rol64(bc[2], 1);
	t[2] = bc[1] ^ rol64(bc[3], 1);
	t[3] = bc[2] ^ rol64(bc[4], 1);
	t[4] = bc[3] ^ rol64(bc[0], 1);

	st[0] ^= t[0];

	/* Rho Pi */
	tt = st[1];
	st[ 1] = rol64(st[ 6] ^ t[1], 44);
	st[ 6] = rol64(st[ 9] ^ t[4], 20);
	st[ 9] = rol64(st[22] ^ t[2], 61);
	st[22] = rol64(st[14] ^ t[4], 39);
	st[14] = rol64(st[20] ^ t[0], 18);
	st[20] = rol64(st[ 2] ^ t[2], 62);
	st[ 2] = rol64(st[12] ^ t[2], 43);
	st[12] = rol64(st[13] ^ t[3], 25);
	st[13] = rol64(st[19] ^ t[4],  8);
	st[19] = rol64(st[23] ^ t[3], 56);
	st[23] = rol64(st[15] ^ t[0], 41);
	st[15] = rol64(st[ 4] ^ t[4], 27);
	st[ 4] = rol64(st[24] ^ t[4], 14);
	st[24] = rol64(st[21] ^ t[1],  2);
	st[21] = rol64(st[ 8] ^ t[3], 55);
	st[ 8] = rol64(st[16] ^ t[1], 45);
	st[16] = rol64(st[ 5] ^ t[0], 36);
	st[ 5] = rol64(st[ 3] ^ t[3], 28);
	st[ 3] = rol64(st[18] ^ t[3], 21);
	st[18] = rol64(st[17] ^ t[2], 15);
	st[17] = rol64(st[11] ^ t[1], 10);
	st[11] = rol64(st[ 7] ^ t[2],  6);
	st[ 7] = rol64(st[10] ^ t[0],  3);
	st[10] = rol64(    tt ^ t[1],  1);

	/* Chi */
	bc[ 0] = ~st[ 1] & st[ 2];
	bc[ 1] = ~st[ 2] & st[ 3];
	bc[ 2] = ~st[ 3] & st[ 4];
	bc[ 3] = ~st[ 4] & st[ 0];
	bc[ 4] = ~st[ 0] & st[ 1];
	st[ 0] ^= bc[ 0];
	st[ 1] ^= bc[ 1];
	st[ 2] ^= bc[ 2];
	st[ 3] ^= bc[ 3];
	st[ 4] ^= bc[ 4];

	bc[ 0] = ~st[ 6] & st[ 7];
	bc[ 1] = ~st[ 7] & st[ 8];
	bc[ 2] = ~st[ 8] & st[ 9];
	bc[ 3] = ~st[ 9] & st[ 5];
	bc[ 4] = ~st[ 5] & st[ 6];
	st[ 5] ^= bc[ 0];
	st[ 6] ^= bc[ 1];
	st[ 7] ^= bc[ 2];
	st[ 8] ^= bc[ 3];
	st[ 9] ^= bc[ 4];

	bc[ 0] = ~st[11] & st[12];
	bc[ 1] = ~st[12] & st[13];
	bc[ 2] = ~st[13] & st[14];
	bc[ 3] = ~st[14] & st[10];
	bc[ 4] = ~st[10] & st[11];
	st[10] ^= bc[ 0];
	st[11] ^= bc[ 1];
	st[12] ^= bc[ 2];
	st[13] ^= bc[ 3];
	st[14] ^= bc[ 4];

	bc[ 0] = ~st[16] & st[17];
	bc[ 1] = ~st[17] & st[18];
	bc[ 2] = ~st[18] & st[19];
	bc[ 3] = ~st[19] & st[15];
	bc[ 4] = ~st[15] & st[16];
	st[15] ^= bc[ 0];
	st[16] ^= bc[ 1];
	st[17] ^= bc[ 2];
	st[18] ^= bc[ 3];
	st[19] ^= bc[ 4];

	bc[ 0] = ~st[21] & st[22];
	bc[ 1] = ~st[22] & st[23];
	bc[ 2] = ~st[23] & st[24];
	bc[ 3] = ~st[24] & st[20];
	bc[ 4] = ~st[20] & st[21];
	st[20] ^= bc[ 0];
	st[21] ^= bc[ 1];
	st[22] ^= bc[ 2];
	st[23] ^= bc[ 3];
	st[24] ^= bc[ 4];
}

static void keccakf(u64 st[25])
{
	int round;

	for (round = 0; round < KECCAK_ROUNDS; round++) {
		keccakf_round(st);
		/* Iota */
		st[0] ^= keccakf_rndc[round];
	}
}

int crypto_sha3_init(struct shash_desc *desc)
{
	struct sha3_state *sctx = shash_desc_ctx(desc);
	unsigned int digest_size = crypto_shash_digestsize(desc->tfm);

	sctx->rsiz = 200 - 2 * digest_size;
	sctx->rsizw = sctx->rsiz / 8;
	sctx->partial = 0;

	memset(sctx->st, 0, sizeof(sctx->st));
	return 0;
}
EXPORT_SYMBOL(crypto_sha3_init);

int crypto_sha3_update(struct shash_desc *desc, const u8 *data,
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
				sctx->st[i] ^= get_unaligned_le64(src + 8 * i);
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
EXPORT_SYMBOL(crypto_sha3_update);

int crypto_sha3_final(struct shash_desc *desc, u8 *out)
{
	struct sha3_state *sctx = shash_desc_ctx(desc);
	unsigned int i, inlen = sctx->partial;
	unsigned int digest_size = crypto_shash_digestsize(desc->tfm);
	__le64 *digest = (__le64 *)out;

	sctx->buf[inlen++] = 0x06;
	memset(sctx->buf + inlen, 0, sctx->rsiz - inlen);
	sctx->buf[sctx->rsiz - 1] |= 0x80;

	for (i = 0; i < sctx->rsizw; i++)
		sctx->st[i] ^= get_unaligned_le64(sctx->buf + 8 * i);

	keccakf(sctx->st);

	for (i = 0; i < digest_size / 8; i++)
		put_unaligned_le64(sctx->st[i], digest++);

	if (digest_size & 4)
		put_unaligned_le32(sctx->st[i], (__le32 *)digest);

	memset(sctx, 0, sizeof(*sctx));
	return 0;
}
EXPORT_SYMBOL(crypto_sha3_final);

static struct shash_alg algs[] = { {
	.digestsize		= SHA3_224_DIGEST_SIZE,
	.init			= crypto_sha3_init,
	.update			= crypto_sha3_update,
	.final			= crypto_sha3_final,
	.descsize		= sizeof(struct sha3_state),
	.base.cra_name		= "sha3-224",
	.base.cra_driver_name	= "sha3-224-generic",
	.base.cra_blocksize	= SHA3_224_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
}, {
	.digestsize		= SHA3_256_DIGEST_SIZE,
	.init			= crypto_sha3_init,
	.update			= crypto_sha3_update,
	.final			= crypto_sha3_final,
	.descsize		= sizeof(struct sha3_state),
	.base.cra_name		= "sha3-256",
	.base.cra_driver_name	= "sha3-256-generic",
	.base.cra_blocksize	= SHA3_256_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
}, {
	.digestsize		= SHA3_384_DIGEST_SIZE,
	.init			= crypto_sha3_init,
	.update			= crypto_sha3_update,
	.final			= crypto_sha3_final,
	.descsize		= sizeof(struct sha3_state),
	.base.cra_name		= "sha3-384",
	.base.cra_driver_name	= "sha3-384-generic",
	.base.cra_blocksize	= SHA3_384_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
}, {
	.digestsize		= SHA3_512_DIGEST_SIZE,
	.init			= crypto_sha3_init,
	.update			= crypto_sha3_update,
	.final			= crypto_sha3_final,
	.descsize		= sizeof(struct sha3_state),
	.base.cra_name		= "sha3-512",
	.base.cra_driver_name	= "sha3-512-generic",
	.base.cra_blocksize	= SHA3_512_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
} };

static int __init sha3_generic_mod_init(void)
{
	return crypto_register_shashes(algs, ARRAY_SIZE(algs));
}

static void __exit sha3_generic_mod_fini(void)
{
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
}

subsys_initcall(sha3_generic_mod_init);
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
