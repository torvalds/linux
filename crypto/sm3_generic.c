// SPDX-License-Identifier: GPL-2.0-only
/*
 * SM3 secure hash, as specified by OSCCA GM/T 0004-2012 SM3 and
 * described at https://tools.ietf.org/html/draft-shen-sm3-hash-01
 *
 * Copyright (C) 2017 ARM Limited or its affiliates.
 * Written by Gilad Ben-Yossef <gilad@benyossef.com>
 * Copyright (C) 2021 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#include <crypto/internal/hash.h>
#include <crypto/sm3.h>
#include <crypto/sm3_base.h>
#include <linux/kernel.h>
#include <linux/module.h>

static int crypto_sm3_update(struct shash_desc *desc, const u8 *data,
			  unsigned int len)
{
	return sm3_base_do_update_blocks(desc, data, len, sm3_block_generic);
}

static int crypto_sm3_finup(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *hash)
{
	sm3_base_do_finup(desc, data, len, sm3_block_generic);
	return sm3_base_finish(desc, hash);
}

static struct shash_alg sm3_alg = {
	.digestsize	=	SM3_DIGEST_SIZE,
	.init		=	sm3_base_init,
	.update		=	crypto_sm3_update,
	.finup		=	crypto_sm3_finup,
	.descsize	=	SM3_STATE_SIZE,
	.base		=	{
		.cra_name	 =	"sm3",
		.cra_driver_name =	"sm3-generic",
		.cra_priority	=	100,
		.cra_flags	 =	CRYPTO_AHASH_ALG_BLOCK_ONLY |
					CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_blocksize	 =	SM3_BLOCK_SIZE,
		.cra_module	 =	THIS_MODULE,
	}
};

static int __init sm3_generic_mod_init(void)
{
	return crypto_register_shash(&sm3_alg);
}

static void __exit sm3_generic_mod_fini(void)
{
	crypto_unregister_shash(&sm3_alg);
}

module_init(sm3_generic_mod_init);
module_exit(sm3_generic_mod_fini);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SM3 Secure Hash Algorithm");

MODULE_ALIAS_CRYPTO("sm3");
MODULE_ALIAS_CRYPTO("sm3-generic");
