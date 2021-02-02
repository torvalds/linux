// SPDX-License-Identifier: GPL-2.0-only
/* Glue code for SHA1 hashing optimized for sparc64 crypto opcodes.
 *
 * This is based largely upon arch/x86/crypto/sha1_ssse3_glue.c
 *
 * Copyright (c) Alan Smithee.
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) Jean-Francois Dive <jef@linuxbe.org>
 * Copyright (c) Mathias Krause <minipli@googlemail.com>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <crypto/sha1.h>

#include <asm/pstate.h>
#include <asm/elf.h>

#include "opcodes.h"

asmlinkage void sha1_sparc64_transform(u32 *digest, const char *data,
				       unsigned int rounds);

static int sha1_sparc64_init(struct shash_desc *desc)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);

	*sctx = (struct sha1_state){
		.state = { SHA1_H0, SHA1_H1, SHA1_H2, SHA1_H3, SHA1_H4 },
	};

	return 0;
}

static void __sha1_sparc64_update(struct sha1_state *sctx, const u8 *data,
				  unsigned int len, unsigned int partial)
{
	unsigned int done = 0;

	sctx->count += len;
	if (partial) {
		done = SHA1_BLOCK_SIZE - partial;
		memcpy(sctx->buffer + partial, data, done);
		sha1_sparc64_transform(sctx->state, sctx->buffer, 1);
	}
	if (len - done >= SHA1_BLOCK_SIZE) {
		const unsigned int rounds = (len - done) / SHA1_BLOCK_SIZE;

		sha1_sparc64_transform(sctx->state, data + done, rounds);
		done += rounds * SHA1_BLOCK_SIZE;
	}

	memcpy(sctx->buffer, data + done, len - done);
}

static int sha1_sparc64_update(struct shash_desc *desc, const u8 *data,
			       unsigned int len)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);
	unsigned int partial = sctx->count % SHA1_BLOCK_SIZE;

	/* Handle the fast case right here */
	if (partial + len < SHA1_BLOCK_SIZE) {
		sctx->count += len;
		memcpy(sctx->buffer + partial, data, len);
	} else
		__sha1_sparc64_update(sctx, data, len, partial);

	return 0;
}

/* Add padding and return the message digest. */
static int sha1_sparc64_final(struct shash_desc *desc, u8 *out)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);
	unsigned int i, index, padlen;
	__be32 *dst = (__be32 *)out;
	__be64 bits;
	static const u8 padding[SHA1_BLOCK_SIZE] = { 0x80, };

	bits = cpu_to_be64(sctx->count << 3);

	/* Pad out to 56 mod 64 and append length */
	index = sctx->count % SHA1_BLOCK_SIZE;
	padlen = (index < 56) ? (56 - index) : ((SHA1_BLOCK_SIZE+56) - index);

	/* We need to fill a whole block for __sha1_sparc64_update() */
	if (padlen <= 56) {
		sctx->count += padlen;
		memcpy(sctx->buffer + index, padding, padlen);
	} else {
		__sha1_sparc64_update(sctx, padding, padlen, index);
	}
	__sha1_sparc64_update(sctx, (const u8 *)&bits, sizeof(bits), 56);

	/* Store state in digest */
	for (i = 0; i < 5; i++)
		dst[i] = cpu_to_be32(sctx->state[i]);

	/* Wipe context */
	memset(sctx, 0, sizeof(*sctx));

	return 0;
}

static int sha1_sparc64_export(struct shash_desc *desc, void *out)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);

	memcpy(out, sctx, sizeof(*sctx));

	return 0;
}

static int sha1_sparc64_import(struct shash_desc *desc, const void *in)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);

	memcpy(sctx, in, sizeof(*sctx));

	return 0;
}

static struct shash_alg alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_sparc64_init,
	.update		=	sha1_sparc64_update,
	.final		=	sha1_sparc64_final,
	.export		=	sha1_sparc64_export,
	.import		=	sha1_sparc64_import,
	.descsize	=	sizeof(struct sha1_state),
	.statesize	=	sizeof(struct sha1_state),
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name=	"sha1-sparc64",
		.cra_priority	=	SPARC_CR_OPCODE_PRIORITY,
		.cra_blocksize	=	SHA1_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static bool __init sparc64_has_sha1_opcode(void)
{
	unsigned long cfr;

	if (!(sparc64_elf_hwcap & HWCAP_SPARC_CRYPTO))
		return false;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
	if (!(cfr & CFR_SHA1))
		return false;

	return true;
}

static int __init sha1_sparc64_mod_init(void)
{
	if (sparc64_has_sha1_opcode()) {
		pr_info("Using sparc64 sha1 opcode optimized SHA-1 implementation\n");
		return crypto_register_shash(&alg);
	}
	pr_info("sparc64 sha1 opcode not available.\n");
	return -ENODEV;
}

static void __exit sha1_sparc64_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(sha1_sparc64_mod_init);
module_exit(sha1_sparc64_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA1 Secure Hash Algorithm, sparc64 sha1 opcode accelerated");

MODULE_ALIAS_CRYPTO("sha1");

#include "crop_devid.c"
