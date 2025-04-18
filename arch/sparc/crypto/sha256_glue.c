// SPDX-License-Identifier: GPL-2.0-only
/* Glue code for SHA256 hashing optimized for sparc64 crypto opcodes.
 *
 * This is based largely upon crypto/sha256_generic.c
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * SHA224 Support Copyright 2007 Intel Corporation <jonathan.lynch@intel.com>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <asm/elf.h>
#include <asm/pstate.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <crypto/sha256_base.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "opcodes.h"

asmlinkage void sha256_sparc64_transform(u32 *digest, const char *data,
					 unsigned int rounds);

static void sha256_block(struct crypto_sha256_state *sctx, const u8 *src,
			 int blocks)
{
	sha256_sparc64_transform(sctx->state, src, blocks);
}

static int sha256_sparc64_update(struct shash_desc *desc, const u8 *data,
				 unsigned int len)
{
	return sha256_base_do_update_blocks(desc, data, len, sha256_block);
}

static int sha256_sparc64_finup(struct shash_desc *desc, const u8 *src,
				unsigned int len, u8 *out)
{
	sha256_base_do_finup(desc, src, len, sha256_block);
	return sha256_base_finish(desc, out);
}

static struct shash_alg sha256_alg = {
	.digestsize	=	SHA256_DIGEST_SIZE,
	.init		=	sha256_base_init,
	.update		=	sha256_sparc64_update,
	.finup		=	sha256_sparc64_finup,
	.descsize	=	sizeof(struct crypto_sha256_state),
	.base		=	{
		.cra_name	=	"sha256",
		.cra_driver_name=	"sha256-sparc64",
		.cra_priority	=	SPARC_CR_OPCODE_PRIORITY,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY |
					CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize	=	SHA256_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static struct shash_alg sha224_alg = {
	.digestsize	=	SHA224_DIGEST_SIZE,
	.init		=	sha224_base_init,
	.update		=	sha256_sparc64_update,
	.finup		=	sha256_sparc64_finup,
	.descsize	=	sizeof(struct crypto_sha256_state),
	.base		=	{
		.cra_name	=	"sha224",
		.cra_driver_name=	"sha224-sparc64",
		.cra_priority	=	SPARC_CR_OPCODE_PRIORITY,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY |
					CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize	=	SHA224_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static bool __init sparc64_has_sha256_opcode(void)
{
	unsigned long cfr;

	if (!(sparc64_elf_hwcap & HWCAP_SPARC_CRYPTO))
		return false;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
	if (!(cfr & CFR_SHA256))
		return false;

	return true;
}

static int __init sha256_sparc64_mod_init(void)
{
	if (sparc64_has_sha256_opcode()) {
		int ret = crypto_register_shash(&sha224_alg);
		if (ret < 0)
			return ret;

		ret = crypto_register_shash(&sha256_alg);
		if (ret < 0) {
			crypto_unregister_shash(&sha224_alg);
			return ret;
		}

		pr_info("Using sparc64 sha256 opcode optimized SHA-256/SHA-224 implementation\n");
		return 0;
	}
	pr_info("sparc64 sha256 opcode not available.\n");
	return -ENODEV;
}

static void __exit sha256_sparc64_mod_fini(void)
{
	crypto_unregister_shash(&sha224_alg);
	crypto_unregister_shash(&sha256_alg);
}

module_init(sha256_sparc64_mod_init);
module_exit(sha256_sparc64_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA-224 and SHA-256 Secure Hash Algorithm, sparc64 sha256 opcode accelerated");

MODULE_ALIAS_CRYPTO("sha224");
MODULE_ALIAS_CRYPTO("sha256");

#include "crop_devid.c"
