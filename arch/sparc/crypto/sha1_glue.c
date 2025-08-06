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

#include <asm/elf.h>
#include <asm/opcodes.h>
#include <asm/pstate.h>
#include <crypto/internal/hash.h>
#include <crypto/sha1.h>
#include <crypto/sha1_base.h>
#include <linux/kernel.h>
#include <linux/module.h>

asmlinkage void sha1_sparc64_transform(struct sha1_state *digest,
				       const u8 *data, int rounds);

static int sha1_sparc64_update(struct shash_desc *desc, const u8 *data,
			       unsigned int len)
{
	return sha1_base_do_update_blocks(desc, data, len,
					  sha1_sparc64_transform);
}

/* Add padding and return the message digest. */
static int sha1_sparc64_finup(struct shash_desc *desc, const u8 *src,
			      unsigned int len, u8 *out)
{
	sha1_base_do_finup(desc, src, len, sha1_sparc64_transform);
	return sha1_base_finish(desc, out);
}

static struct shash_alg alg = {
	.digestsize	=	SHA1_DIGEST_SIZE,
	.init		=	sha1_base_init,
	.update		=	sha1_sparc64_update,
	.finup		=	sha1_sparc64_finup,
	.descsize	=	SHA1_STATE_SIZE,
	.base		=	{
		.cra_name	=	"sha1",
		.cra_driver_name=	"sha1-sparc64",
		.cra_priority	=	SPARC_CR_OPCODE_PRIORITY,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY,
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
