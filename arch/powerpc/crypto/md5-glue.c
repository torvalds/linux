// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue code for MD5 implementation for PPC assembler
 *
 * Based on generic implementation.
 *
 * Copyright (c) 2015 Markus Stockhausen <stockhausen@collogia.de>
 */

#include <crypto/internal/hash.h>
#include <crypto/md5.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

extern void ppc_md5_transform(u32 *state, const u8 *src, u32 blocks);

static int ppc_md5_init(struct shash_desc *desc)
{
	struct md5_state *sctx = shash_desc_ctx(desc);

	sctx->hash[0] = MD5_H0;
	sctx->hash[1] = MD5_H1;
	sctx->hash[2] = MD5_H2;
	sctx->hash[3] =	MD5_H3;
	sctx->byte_count = 0;

	return 0;
}

static int ppc_md5_update(struct shash_desc *desc, const u8 *data,
			unsigned int len)
{
	struct md5_state *sctx = shash_desc_ctx(desc);

	sctx->byte_count += round_down(len, MD5_HMAC_BLOCK_SIZE);
	ppc_md5_transform(sctx->hash, data, len >> 6);
	return len - round_down(len, MD5_HMAC_BLOCK_SIZE);
}

static int ppc_md5_finup(struct shash_desc *desc, const u8 *src,
			 unsigned int offset, u8 *out)
{
	struct md5_state *sctx = shash_desc_ctx(desc);
	__le64 block[MD5_BLOCK_WORDS] = {};
	u8 *p = memcpy(block, src, offset);
	__le32 *dst = (__le32 *)out;
	__le64 *pbits;

	src = p;
	p += offset;
	*p++ = 0x80;
	sctx->byte_count += offset;
	pbits = &block[(MD5_BLOCK_WORDS / (offset > 55 ? 1 : 2)) - 1];
	*pbits = cpu_to_le64(sctx->byte_count << 3);
	ppc_md5_transform(sctx->hash, src, (pbits - block + 1) / 8);
	memzero_explicit(block, sizeof(block));

	dst[0] = cpu_to_le32(sctx->hash[0]);
	dst[1] = cpu_to_le32(sctx->hash[1]);
	dst[2] = cpu_to_le32(sctx->hash[2]);
	dst[3] = cpu_to_le32(sctx->hash[3]);
	return 0;
}

static struct shash_alg alg = {
	.digestsize	=	MD5_DIGEST_SIZE,
	.init		=	ppc_md5_init,
	.update		=	ppc_md5_update,
	.finup		=	ppc_md5_finup,
	.descsize	=	MD5_STATE_SIZE,
	.base		=	{
		.cra_name	=	"md5",
		.cra_driver_name=	"md5-ppc",
		.cra_priority	=	200,
		.cra_flags	=	CRYPTO_AHASH_ALG_BLOCK_ONLY,
		.cra_blocksize	=	MD5_HMAC_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
};

static int __init ppc_md5_mod_init(void)
{
	return crypto_register_shash(&alg);
}

static void __exit ppc_md5_mod_fini(void)
{
	crypto_unregister_shash(&alg);
}

module_init(ppc_md5_mod_init);
module_exit(ppc_md5_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MD5 Secure Hash Algorithm, PPC assembler");

MODULE_ALIAS_CRYPTO("md5");
MODULE_ALIAS_CRYPTO("md5-ppc");
