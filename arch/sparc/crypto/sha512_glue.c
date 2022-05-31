// SPDX-License-Identifier: GPL-2.0-only
/* Glue code for SHA512 hashing optimized for sparc64 crypto opcodes.
 *
 * This is based largely upon crypto/sha512_generic.c
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2003 Kyle McMartin <kyle@debian.org>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <crypto/sha2.h>
#include <crypto/sha512_base.h>

#include <asm/pstate.h>
#include <asm/elf.h>

#include "opcodes.h"

asmlinkage void sha512_sparc64_transform(u64 *digest, const char *data,
					 unsigned int rounds);

static void __sha512_sparc64_update(struct sha512_state *sctx, const u8 *data,
				    unsigned int len, unsigned int partial)
{
	unsigned int done = 0;

	if ((sctx->count[0] += len) < len)
		sctx->count[1]++;
	if (partial) {
		done = SHA512_BLOCK_SIZE - partial;
		memcpy(sctx->buf + partial, data, done);
		sha512_sparc64_transform(sctx->state, sctx->buf, 1);
	}
	if (len - done >= SHA512_BLOCK_SIZE) {
		const unsigned int rounds = (len - done) / SHA512_BLOCK_SIZE;

		sha512_sparc64_transform(sctx->state, data + done, rounds);
		done += rounds * SHA512_BLOCK_SIZE;
	}

	memcpy(sctx->buf, data + done, len - done);
}

static int sha512_sparc64_update(struct shash_desc *desc, const u8 *data,
				 unsigned int len)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);
	unsigned int partial = sctx->count[0] % SHA512_BLOCK_SIZE;

	/* Handle the fast case right here */
	if (partial + len < SHA512_BLOCK_SIZE) {
		if ((sctx->count[0] += len) < len)
			sctx->count[1]++;
		memcpy(sctx->buf + partial, data, len);
	} else
		__sha512_sparc64_update(sctx, data, len, partial);

	return 0;
}

static int sha512_sparc64_final(struct shash_desc *desc, u8 *out)
{
	struct sha512_state *sctx = shash_desc_ctx(desc);
	unsigned int i, index, padlen;
	__be64 *dst = (__be64 *)out;
	__be64 bits[2];
	static const u8 padding[SHA512_BLOCK_SIZE] = { 0x80, };

	/* Save number of bits */
	bits[1] = cpu_to_be64(sctx->count[0] << 3);
	bits[0] = cpu_to_be64(sctx->count[1] << 3 | sctx->count[0] >> 61);

	/* Pad out to 112 mod 128 and append length */
	index = sctx->count[0] % SHA512_BLOCK_SIZE;
	padlen = (index < 112) ? (112 - index) : ((SHA512_BLOCK_SIZE+112) - index);

	/* We need to fill a whole block for __sha512_sparc64_update() */
	if (padlen <= 112) {
		if ((sctx->count[0] += padlen) < padlen)
			sctx->count[1]++;
		memcpy(sctx->buf + index, padding, padlen);
	} else {
		__sha512_sparc64_update(sctx, padding, padlen, index);
	}
	__sha512_sparc64_update(sctx, (const u8 *)&bits, sizeof(bits), 112);

	/* Store state in digest */
	for (i = 0; i < 8; i++)
		dst[i] = cpu_to_be64(sctx->state[i]);

	/* Wipe context */
	memset(sctx, 0, sizeof(*sctx));

	return 0;
}

static int sha384_sparc64_final(struct shash_desc *desc, u8 *hash)
{
	u8 D[64];

	sha512_sparc64_final(desc, D);

	memcpy(hash, D, 48);
	memzero_explicit(D, 64);

	return 0;
}

static struct shash_alg sha512 = {
	.digestsize	=	SHA512_DIGEST_SIZE,
	.init		=	sha512_base_init,
	.update		=	sha512_sparc64_update,
	.final		=	sha512_sparc64_final,
	.descsize	=	sizeof(struct sha512_state),
	.base		=	{
		.cra_name	=	"sha512",
		.cra_driver_name=	"sha512-sparc64",
		.cra_priority	=	SPARC_CR_OPCODE_PRIORITY,
		.cra_blocksize	=	SHA512_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static struct shash_alg sha384 = {
	.digestsize	=	SHA384_DIGEST_SIZE,
	.init		=	sha384_base_init,
	.update		=	sha512_sparc64_update,
	.final		=	sha384_sparc64_final,
	.descsize	=	sizeof(struct sha512_state),
	.base		=	{
		.cra_name	=	"sha384",
		.cra_driver_name=	"sha384-sparc64",
		.cra_priority	=	SPARC_CR_OPCODE_PRIORITY,
		.cra_blocksize	=	SHA384_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static bool __init sparc64_has_sha512_opcode(void)
{
	unsigned long cfr;

	if (!(sparc64_elf_hwcap & HWCAP_SPARC_CRYPTO))
		return false;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
	if (!(cfr & CFR_SHA512))
		return false;

	return true;
}

static int __init sha512_sparc64_mod_init(void)
{
	if (sparc64_has_sha512_opcode()) {
		int ret = crypto_register_shash(&sha384);
		if (ret < 0)
			return ret;

		ret = crypto_register_shash(&sha512);
		if (ret < 0) {
			crypto_unregister_shash(&sha384);
			return ret;
		}

		pr_info("Using sparc64 sha512 opcode optimized SHA-512/SHA-384 implementation\n");
		return 0;
	}
	pr_info("sparc64 sha512 opcode not available.\n");
	return -ENODEV;
}

static void __exit sha512_sparc64_mod_fini(void)
{
	crypto_unregister_shash(&sha384);
	crypto_unregister_shash(&sha512);
}

module_init(sha512_sparc64_mod_init);
module_exit(sha512_sparc64_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA-384 and SHA-512 Secure Hash Algorithm, sparc64 sha512 opcode accelerated");

MODULE_ALIAS_CRYPTO("sha384");
MODULE_ALIAS_CRYPTO("sha512");

#include "crop_devid.c"
