// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue code for SHA-1 implementation for SPE instructions (PPC)
 *
 * Based on generic implementation.
 *
 * Copyright (c) 2015 Markus Stockhausen <stockhausen@collogia.de>
 */

#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <crypto/sha1.h>
#include <crypto/sha1_base.h>
#include <asm/byteorder.h>
#include <asm/switch_to.h>
#include <linux/hardirq.h>

/*
 * MAX_BYTES defines the number of bytes that are allowed to be processed
 * between preempt_disable() and preempt_enable(). SHA1 takes ~1000
 * operations per 64 bytes. e500 cores can issue two arithmetic instructions
 * per clock cycle using one 32/64 bit unit (SU1) and one 32 bit unit (SU2).
 * Thus 2KB of input data will need an estimated maximum of 18,000 cycles.
 * Headroom for cache misses included. Even with the low end model clocked
 * at 667 MHz this equals to a critical time window of less than 27us.
 *
 */
#define MAX_BYTES 2048

extern void ppc_spe_sha1_transform(u32 *state, const u8 *src, u32 blocks);

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

static inline void ppc_sha1_clear_context(struct sha1_state *sctx)
{
	int count = sizeof(struct sha1_state) >> 2;
	u32 *ptr = (u32 *)sctx;

	/* make sure we can clear the fast way */
	BUILD_BUG_ON(sizeof(struct sha1_state) % 4);
	do { *ptr++ = 0; } while (--count);
}

static int ppc_spe_sha1_update(struct shash_desc *desc, const u8 *data,
			unsigned int len)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);
	const unsigned int offset = sctx->count & 0x3f;
	const unsigned int avail = 64 - offset;
	unsigned int bytes;
	const u8 *src = data;

	if (avail > len) {
		sctx->count += len;
		memcpy((char *)sctx->buffer + offset, src, len);
		return 0;
	}

	sctx->count += len;

	if (offset) {
		memcpy((char *)sctx->buffer + offset, src, avail);

		spe_begin();
		ppc_spe_sha1_transform(sctx->state, (const u8 *)sctx->buffer, 1);
		spe_end();

		len -= avail;
		src += avail;
	}

	while (len > 63) {
		bytes = (len > MAX_BYTES) ? MAX_BYTES : len;
		bytes = bytes & ~0x3f;

		spe_begin();
		ppc_spe_sha1_transform(sctx->state, src, bytes >> 6);
		spe_end();

		src += bytes;
		len -= bytes;
	}

	memcpy((char *)sctx->buffer, src, len);
	return 0;
}

static int ppc_spe_sha1_final(struct shash_desc *desc, u8 *out)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);
	const unsigned int offset = sctx->count & 0x3f;
	char *p = (char *)sctx->buffer + offset;
	int padlen;
	__be64 *pbits = (__be64 *)(((char *)&sctx->buffer) + 56);
	__be32 *dst = (__be32 *)out;

	padlen = 55 - offset;
	*p++ = 0x80;

	spe_begin();

	if (padlen < 0) {
		memset(p, 0x00, padlen + sizeof (u64));
		ppc_spe_sha1_transform(sctx->state, sctx->buffer, 1);
		p = (char *)sctx->buffer;
		padlen = 56;
	}

	memset(p, 0, padlen);
	*pbits = cpu_to_be64(sctx->count << 3);
	ppc_spe_sha1_transform(sctx->state, sctx->buffer, 1);

	spe_end();

	dst[0] = cpu_to_be32(sctx->state[0]);
	dst[1] = cpu_to_be32(sctx->state[1]);
	dst[2] = cpu_to_be32(sctx->state[2]);
	dst[3] = cpu_to_be32(sctx->state[3]);
	dst[4] = cpu_to_be32(sctx->state[4]);

	ppc_sha1_clear_context(sctx);
	return 0;
}

static int ppc_spe_sha1_export(struct shash_desc *desc, void *out)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);

	memcpy(out, sctx, sizeof(*sctx));
	return 0;
}

static int ppc_spe_sha1_import(struct shash_desc *desc, const void *in)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);

	memcpy(sctx, in, sizeof(*sctx));
	return 0;
}

static struct shash_alg alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_base_init,
	.update		=	ppc_spe_sha1_update,
	.final		=	ppc_spe_sha1_final,
	.export		=	ppc_spe_sha1_export,
	.import		=	ppc_spe_sha1_import,
	.descsize	=	sizeof(struct sha1_state),
	.statesize	=	sizeof(struct sha1_state),
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name=	"sha1-ppc-spe",
		.cra_priority	=	300,
		.cra_blocksize	=	SHA1_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int __init ppc_spe_sha1_mod_init(void)
{
	return crypto_register_shash(&alg);
}

static void __exit ppc_spe_sha1_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(ppc_spe_sha1_mod_init);
module_exit(ppc_spe_sha1_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm, SPE optimized");

MODULE_ALIAS_CRYPTO("sha1");
MODULE_ALIAS_CRYPTO("sha1-ppc-spe");
