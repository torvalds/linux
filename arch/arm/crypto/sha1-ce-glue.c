/*
 * sha1-ce-glue.c - SHA-1 secure hash using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <crypto/internal/hash.h>
#include <crypto/sha.h>
#include <crypto/sha1_base.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/module.h>

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>

#include "sha1.h"

MODULE_DESCRIPTION("SHA1 secure hash using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

asmlinkage void sha1_ce_transform(struct sha1_state *sst, u8 const *src,
				  int blocks);

static int sha1_ce_update(struct shash_desc *desc, const u8 *data,
			  unsigned int len)
{
	struct sha1_state *sctx = shash_desc_ctx(desc);

	if (!may_use_simd() ||
	    (sctx->count % SHA1_BLOCK_SIZE) + len < SHA1_BLOCK_SIZE)
		return sha1_update_arm(desc, data, len);

	kernel_neon_begin();
	sha1_base_do_update(desc, data, len, sha1_ce_transform);
	kernel_neon_end();

	return 0;
}

static int sha1_ce_finup(struct shash_desc *desc, const u8 *data,
			 unsigned int len, u8 *out)
{
	if (!may_use_simd())
		return sha1_finup_arm(desc, data, len, out);

	kernel_neon_begin();
	if (len)
		sha1_base_do_update(desc, data, len, sha1_ce_transform);
	sha1_base_do_finalize(desc, sha1_ce_transform);
	kernel_neon_end();

	return sha1_base_finish(desc, out);
}

static int sha1_ce_final(struct shash_desc *desc, u8 *out)
{
	return sha1_ce_finup(desc, NULL, 0, out);
}

static struct shash_alg alg = {
	.init			= sha1_base_init,
	.update			= sha1_ce_update,
	.final			= sha1_ce_final,
	.finup			= sha1_ce_finup,
	.descsize		= sizeof(struct sha1_state),
	.digestsize		= SHA1_DIGEST_SIZE,
	.base			= {
		.cra_name		= "sha1",
		.cra_driver_name	= "sha1-ce",
		.cra_priority		= 200,
		.cra_blocksize		= SHA1_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	}
};

static int __init sha1_ce_mod_init(void)
{
	return crypto_register_shash(&alg);
}

static void __exit sha1_ce_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_cpu_feature_match(SHA1, sha1_ce_mod_init);
module_exit(sha1_ce_mod_fini);
