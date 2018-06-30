/*
 * Glue code for SHA-256 implementation for SPE instructions (PPC)
 *
 * Based on generic implementation. The assembler module takes care 
 * about the SPE registers so it can run from interrupt context.
 *
 * Copyright (c) 2015 Markus Stockhausen <stockhausen@collogia.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/cryptohash.h>
#include <linux/types.h>
#include <crypto/sha.h>
#include <asm/byteorder.h>
#include <asm/switch_to.h>
#include <linux/hardirq.h>

/*
 * MAX_BYTES defines the number of bytes that are allowed to be processed
 * between preempt_disable() and preempt_enable(). SHA256 takes ~2,000
 * operations per 64 bytes. e500 cores can issue two arithmetic instructions
 * per clock cycle using one 32/64 bit unit (SU1) and one 32 bit unit (SU2).
 * Thus 1KB of input data will need an estimated maximum of 18,000 cycles.
 * Headroom for cache misses included. Even with the low end model clocked
 * at 667 MHz this equals to a critical time window of less than 27us.
 *
 */
#define MAX_BYTES 1024

extern void ppc_spe_sha256_transform(u32 *state, const u8 *src, u32 blocks);

static void spe_begin(void)
{
	/* We just start SPE operations and will save SPE registers later. */
	preempt_disable();
	enable_kernel_spe();
}

static void spe_end(void)
{
	disable_kernel_spe();
	/* reenable preemption */
	preempt_enable();
}

static inline void ppc_sha256_clear_context(struct sha256_state *sctx)
{
	int count = sizeof(struct sha256_state) >> 2;
	u32 *ptr = (u32 *)sctx;

	/* make sure we can clear the fast way */
	BUILD_BUG_ON(sizeof(struct sha256_state) % 4);
	do { *ptr++ = 0; } while (--count);
}

static int ppc_spe_sha256_init(struct shash_desc *desc)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SHA256_H0;
	sctx->state[1] = SHA256_H1;
	sctx->state[2] = SHA256_H2;
	sctx->state[3] = SHA256_H3;
	sctx->state[4] = SHA256_H4;
	sctx->state[5] = SHA256_H5;
	sctx->state[6] = SHA256_H6;
	sctx->state[7] = SHA256_H7;
	sctx->count = 0;

	return 0;
}

static int ppc_spe_sha224_init(struct shash_desc *desc)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	sctx->state[0] = SHA224_H0;
	sctx->state[1] = SHA224_H1;
	sctx->state[2] = SHA224_H2;
	sctx->state[3] = SHA224_H3;
	sctx->state[4] = SHA224_H4;
	sctx->state[5] = SHA224_H5;
	sctx->state[6] = SHA224_H6;
	sctx->state[7] = SHA224_H7;
	sctx->count = 0;

	return 0;
}

static int ppc_spe_sha256_update(struct shash_desc *desc, const u8 *data,
			unsigned int len)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	const unsigned int offset = sctx->count & 0x3f;
	const unsigned int avail = 64 - offset;
	unsigned int bytes;
	const u8 *src = data;

	if (avail > len) {
		sctx->count += len;
		memcpy((char *)sctx->buf + offset, src, len);
		return 0;
	}

	sctx->count += len;

	if (offset) {
		memcpy((char *)sctx->buf + offset, src, avail);

		spe_begin();
		ppc_spe_sha256_transform(sctx->state, (const u8 *)sctx->buf, 1);
		spe_end();

		len -= avail;
		src += avail;
	}

	while (len > 63) {
		/* cut input data into smaller blocks */
		bytes = (len > MAX_BYTES) ? MAX_BYTES : len;
		bytes = bytes & ~0x3f;

		spe_begin();
		ppc_spe_sha256_transform(sctx->state, src, bytes >> 6);
		spe_end();

		src += bytes;
		len -= bytes;
	};

	memcpy((char *)sctx->buf, src, len);
	return 0;
}

static int ppc_spe_sha256_final(struct shash_desc *desc, u8 *out)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);
	const unsigned int offset = sctx->count & 0x3f;
	char *p = (char *)sctx->buf + offset;
	int padlen;
	__be64 *pbits = (__be64 *)(((char *)&sctx->buf) + 56);
	__be32 *dst = (__be32 *)out;

	padlen = 55 - offset;
	*p++ = 0x80;

	spe_begin();

	if (padlen < 0) {
		memset(p, 0x00, padlen + sizeof (u64));
		ppc_spe_sha256_transform(sctx->state, sctx->buf, 1);
		p = (char *)sctx->buf;
		padlen = 56;
	}

	memset(p, 0, padlen);
	*pbits = cpu_to_be64(sctx->count << 3);
	ppc_spe_sha256_transform(sctx->state, sctx->buf, 1);

	spe_end();

	dst[0] = cpu_to_be32(sctx->state[0]);
	dst[1] = cpu_to_be32(sctx->state[1]);
	dst[2] = cpu_to_be32(sctx->state[2]);
	dst[3] = cpu_to_be32(sctx->state[3]);
	dst[4] = cpu_to_be32(sctx->state[4]);
	dst[5] = cpu_to_be32(sctx->state[5]);
	dst[6] = cpu_to_be32(sctx->state[6]);
	dst[7] = cpu_to_be32(sctx->state[7]);

	ppc_sha256_clear_context(sctx);
	return 0;
}

static int ppc_spe_sha224_final(struct shash_desc *desc, u8 *out)
{
	u32 D[SHA256_DIGEST_SIZE >> 2];
	__be32 *dst = (__be32 *)out;

	ppc_spe_sha256_final(desc, (u8 *)D);

	/* avoid bytewise memcpy */
	dst[0] = D[0];
	dst[1] = D[1];
	dst[2] = D[2];
	dst[3] = D[3];
	dst[4] = D[4];
	dst[5] = D[5];
	dst[6] = D[6];

	/* clear sensitive data */
	memzero_explicit(D, SHA256_DIGEST_SIZE);
	return 0;
}

static int ppc_spe_sha256_export(struct shash_desc *desc, void *out)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	memcpy(out, sctx, sizeof(*sctx));
	return 0;
}

static int ppc_spe_sha256_import(struct shash_desc *desc, const void *in)
{
	struct sha256_state *sctx = shash_desc_ctx(desc);

	memcpy(sctx, in, sizeof(*sctx));
	return 0;
}

static struct shash_alg algs[2] = { {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	ppc_spe_sha256_init,
	.update		=	ppc_spe_sha256_update,
	.final		=	ppc_spe_sha256_final,
	.export		=	ppc_spe_sha256_export,
	.import		=	ppc_spe_sha256_import,
	.descsize	=	sizeof(struct sha256_state),
	.statesize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name=	"sha256-ppc-spe",
		.cra_priority	=	300,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	SHA224_DIGEST_SIZE,
	.init		=	ppc_spe_sha224_init,
	.update		=	ppc_spe_sha256_update,
	.final		=	ppc_spe_sha224_final,
	.export		=	ppc_spe_sha256_export,
	.import		=	ppc_spe_sha256_import,
	.descsize	=	sizeof(struct sha256_state),
	.statesize	=	sizeof(struct sha256_state),
	.base		=	{
		.cra_name	=	"sha224",
		.cra_driver_name=	"sha224-ppc-spe",
		.cra_priority	=	300,
		.cra_blocksize	=	SHA224_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };

static int __init ppc_spe_sha256_mod_init(void)
{
	return crypto_register_shashes(algs, ARRAY_SIZE(algs));
}

static void __exit ppc_spe_sha256_mod_fini(void)
{
	crypto_unregister_shashes(algs, ARRAY_SIZE(algs));
}

module_init(ppc_spe_sha256_mod_init);
module_exit(ppc_spe_sha256_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA-224 and SHA-256 Secure Hash Algorithm, SPE optimized");

MODULE_ALIAS_CRYPTO("sha224");
MODULE_ALIAS_CRYPTO("sha224-ppc-spe");
MODULE_ALIAS_CRYPTO("sha256");
MODULE_ALIAS_CRYPTO("sha256-ppc-spe");
