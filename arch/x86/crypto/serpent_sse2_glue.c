/*
 * Glue Code for SSE2 assembler versions of Serpent Cipher
 *
 * Copyright (c) 2011 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 *
 * Glue code based on aesni-intel_glue.c by:
 *  Copyright (C) 2008, Intel Corp.
 *    Author: Huang Ying <ying.huang@intel.com>
 *
 * CBC & ECB parts based on code (crypto/cbc.c,ecb.c) by:
 *   Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 * CTR part based on code (crypto/ctr.c) by:
 *   (C) Copyright IBM Corp. 2007 - Joy Latten <latten@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <crypto/algapi.h>
#include <crypto/b128ops.h>
#include <crypto/internal/simd.h>
#include <crypto/serpent.h>
#include <asm/crypto/serpent-sse2.h>
#include <asm/crypto/glue_helper.h>

static int serpent_setkey_skcipher(struct crypto_skcipher *tfm,
				   const u8 *key, unsigned int keylen)
{
	return __serpent_setkey(crypto_skcipher_ctx(tfm), key, keylen);
}

static void serpent_decrypt_cbc_xway(void *ctx, u128 *dst, const u128 *src)
{
	u128 ivs[SERPENT_PARALLEL_BLOCKS - 1];
	unsigned int j;

	for (j = 0; j < SERPENT_PARALLEL_BLOCKS - 1; j++)
		ivs[j] = src[j];

	serpent_dec_blk_xway(ctx, (u8 *)dst, (u8 *)src);

	for (j = 0; j < SERPENT_PARALLEL_BLOCKS - 1; j++)
		u128_xor(dst + (j + 1), dst + (j + 1), ivs + j);
}

static void serpent_crypt_ctr(void *ctx, u128 *dst, const u128 *src, le128 *iv)
{
	be128 ctrblk;

	le128_to_be128(&ctrblk, iv);
	le128_inc(iv);

	__serpent_encrypt(ctx, (u8 *)&ctrblk, (u8 *)&ctrblk);
	u128_xor(dst, src, (u128 *)&ctrblk);
}

static void serpent_crypt_ctr_xway(void *ctx, u128 *dst, const u128 *src,
				   le128 *iv)
{
	be128 ctrblks[SERPENT_PARALLEL_BLOCKS];
	unsigned int i;

	for (i = 0; i < SERPENT_PARALLEL_BLOCKS; i++) {
		if (dst != src)
			dst[i] = src[i];

		le128_to_be128(&ctrblks[i], iv);
		le128_inc(iv);
	}

	serpent_enc_blk_xway_xor(ctx, (u8 *)dst, (u8 *)ctrblks);
}

static const struct common_glue_ctx serpent_enc = {
	.num_funcs = 2,
	.fpu_blocks_limit = SERPENT_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = SERPENT_PARALLEL_BLOCKS,
		.fn_u = { .ecb = GLUE_FUNC_CAST(serpent_enc_blk_xway) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(__serpent_encrypt) }
	} }
};

static const struct common_glue_ctx serpent_ctr = {
	.num_funcs = 2,
	.fpu_blocks_limit = SERPENT_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = SERPENT_PARALLEL_BLOCKS,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(serpent_crypt_ctr_xway) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(serpent_crypt_ctr) }
	} }
};

static const struct common_glue_ctx serpent_dec = {
	.num_funcs = 2,
	.fpu_blocks_limit = SERPENT_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = SERPENT_PARALLEL_BLOCKS,
		.fn_u = { .ecb = GLUE_FUNC_CAST(serpent_dec_blk_xway) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(__serpent_decrypt) }
	} }
};

static const struct common_glue_ctx serpent_dec_cbc = {
	.num_funcs = 2,
	.fpu_blocks_limit = SERPENT_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = SERPENT_PARALLEL_BLOCKS,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(serpent_decrypt_cbc_xway) }
	}, {
		.num_blocks = 1,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(__serpent_decrypt) }
	} }
};

static int ecb_encrypt(struct skcipher_request *req)
{
	return glue_ecb_req_128bit(&serpent_enc, req);
}

static int ecb_decrypt(struct skcipher_request *req)
{
	return glue_ecb_req_128bit(&serpent_dec, req);
}

static int cbc_encrypt(struct skcipher_request *req)
{
	return glue_cbc_encrypt_req_128bit(GLUE_FUNC_CAST(__serpent_encrypt),
					   req);
}

static int cbc_decrypt(struct skcipher_request *req)
{
	return glue_cbc_decrypt_req_128bit(&serpent_dec_cbc, req);
}

static int ctr_crypt(struct skcipher_request *req)
{
	return glue_ctr_req_128bit(&serpent_ctr, req);
}

static struct skcipher_alg serpent_algs[] = {
	{
		.base.cra_name		= "__ecb(serpent)",
		.base.cra_driver_name	= "__ecb-serpent-sse2",
		.base.cra_priority	= 400,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= SERPENT_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct serpent_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= SERPENT_MIN_KEY_SIZE,
		.max_keysize		= SERPENT_MAX_KEY_SIZE,
		.setkey			= serpent_setkey_skcipher,
		.encrypt		= ecb_encrypt,
		.decrypt		= ecb_decrypt,
	}, {
		.base.cra_name		= "__cbc(serpent)",
		.base.cra_driver_name	= "__cbc-serpent-sse2",
		.base.cra_priority	= 400,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= SERPENT_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct serpent_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= SERPENT_MIN_KEY_SIZE,
		.max_keysize		= SERPENT_MAX_KEY_SIZE,
		.ivsize			= SERPENT_BLOCK_SIZE,
		.setkey			= serpent_setkey_skcipher,
		.encrypt		= cbc_encrypt,
		.decrypt		= cbc_decrypt,
	}, {
		.base.cra_name		= "__ctr(serpent)",
		.base.cra_driver_name	= "__ctr-serpent-sse2",
		.base.cra_priority	= 400,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct serpent_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= SERPENT_MIN_KEY_SIZE,
		.max_keysize		= SERPENT_MAX_KEY_SIZE,
		.ivsize			= SERPENT_BLOCK_SIZE,
		.chunksize		= SERPENT_BLOCK_SIZE,
		.setkey			= serpent_setkey_skcipher,
		.encrypt		= ctr_crypt,
		.decrypt		= ctr_crypt,
	},
};

static struct simd_skcipher_alg *serpent_simd_algs[ARRAY_SIZE(serpent_algs)];

static int __init serpent_sse2_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_XMM2)) {
		printk(KERN_INFO "SSE2 instructions are not detected.\n");
		return -ENODEV;
	}

	return simd_register_skciphers_compat(serpent_algs,
					      ARRAY_SIZE(serpent_algs),
					      serpent_simd_algs);
}

static void __exit serpent_sse2_exit(void)
{
	simd_unregister_skciphers(serpent_algs, ARRAY_SIZE(serpent_algs),
				  serpent_simd_algs);
}

module_init(serpent_sse2_init);
module_exit(serpent_sse2_exit);

MODULE_DESCRIPTION("Serpent Cipher Algorithm, SSE2 optimized");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("serpent");
