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

#include <asm/elf.h>
#include <asm/opcodes.h>
#include <asm/pstate.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <crypto/sha512_base.h>
#include <linux/kernel.h>
#include <linux/module.h>

asmlinkage void sha512_sparc64_transform(u64 *digest, const char *data,
					 unsigned int rounds);

static void sha512_block(struct sha512_state *sctx, const u8 *src, int blocks)
{
	sha512_sparc64_transform(sctx->state, src, blocks);
}

static int sha512_sparc64_update(struct shash_desc *desc, const u8 *data,
				 unsigned int len)
{
	return sha512_base_do_update_blocks(desc, data, len, sha512_block);
}

static int sha512_sparc64_finup(struct shash_desc *desc, const u8 *src,
				unsigned int len, u8 *out)
{
	sha512_base_do_finup(desc, src, len, sha512_block);
	return sha512_base_finish(desc, out);
}

static struct shash_alg sha512 = {
	.digestsize	=	SHA512_DIGEST_SIZE,
	.init		=	sha512_base_init,
	.update		=	sha512_sparc64_update,
	.finup		=	sha512_sparc64_finup,
	.descsize	=	SHA512_STATE_SIZE,
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
	.finup		=	sha512_sparc64_finup,
	.descsize	=	SHA512_STATE_SIZE,
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
