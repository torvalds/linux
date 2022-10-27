/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM4 Cipher Algorithm, using ARMv8 Crypto Extensions
 * as specified in
 * https://tools.ietf.org/id/draft-ribose-cfrg-sm4-10.html
 *
 * Copyright (C) 2022, Alibaba Group.
 * Copyright (C) 2022 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/kernel.h>
#include <linux/cpufeature.h>
#include <asm/neon.h>
#include <asm/simd.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <crypto/sm4.h>

#define BYTES2BLKS(nbytes)	((nbytes) >> 4)

asmlinkage void sm4_ce_expand_key(const u8 *key, u32 *rkey_enc, u32 *rkey_dec,
				  const u32 *fk, const u32 *ck);
asmlinkage void sm4_ce_crypt_block(const u32 *rkey, u8 *dst, const u8 *src);
asmlinkage void sm4_ce_crypt(const u32 *rkey, u8 *dst, const u8 *src,
			     unsigned int nblks);
asmlinkage void sm4_ce_cbc_enc(const u32 *rkey, u8 *dst, const u8 *src,
			       u8 *iv, unsigned int nblocks);
asmlinkage void sm4_ce_cbc_dec(const u32 *rkey, u8 *dst, const u8 *src,
			       u8 *iv, unsigned int nblocks);
asmlinkage void sm4_ce_cbc_cts_enc(const u32 *rkey, u8 *dst, const u8 *src,
				   u8 *iv, unsigned int nbytes);
asmlinkage void sm4_ce_cbc_cts_dec(const u32 *rkey, u8 *dst, const u8 *src,
				   u8 *iv, unsigned int nbytes);
asmlinkage void sm4_ce_cfb_enc(const u32 *rkey, u8 *dst, const u8 *src,
			       u8 *iv, unsigned int nblks);
asmlinkage void sm4_ce_cfb_dec(const u32 *rkey, u8 *dst, const u8 *src,
			       u8 *iv, unsigned int nblks);
asmlinkage void sm4_ce_ctr_enc(const u32 *rkey, u8 *dst, const u8 *src,
			       u8 *iv, unsigned int nblks);

EXPORT_SYMBOL(sm4_ce_expand_key);
EXPORT_SYMBOL(sm4_ce_crypt_block);
EXPORT_SYMBOL(sm4_ce_cbc_enc);
EXPORT_SYMBOL(sm4_ce_cfb_enc);

static int sm4_setkey(struct crypto_skcipher *tfm, const u8 *key,
		      unsigned int key_len)
{
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);

	if (key_len != SM4_KEY_SIZE)
		return -EINVAL;

	kernel_neon_begin();
	sm4_ce_expand_key(key, ctx->rkey_enc, ctx->rkey_dec,
			  crypto_sm4_fk, crypto_sm4_ck);
	kernel_neon_end();
	return 0;
}

static int sm4_ecb_do_crypt(struct skcipher_request *req, const u32 *rkey)
{
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) > 0) {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		unsigned int nblks;

		kernel_neon_begin();

		nblks = BYTES2BLKS(nbytes);
		if (nblks) {
			sm4_ce_crypt(rkey, dst, src, nblks);
			nbytes -= nblks * SM4_BLOCK_SIZE;
		}

		kernel_neon_end();

		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int sm4_ecb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);

	return sm4_ecb_do_crypt(req, ctx->rkey_enc);
}

static int sm4_ecb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);

	return sm4_ecb_do_crypt(req, ctx->rkey_dec);
}

static int sm4_cbc_crypt(struct skcipher_request *req,
			 struct sm4_ctx *ctx, bool encrypt)
{
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		return err;

	while ((nbytes = walk.nbytes) > 0) {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		unsigned int nblocks;

		nblocks = nbytes / SM4_BLOCK_SIZE;
		if (nblocks) {
			kernel_neon_begin();

			if (encrypt)
				sm4_ce_cbc_enc(ctx->rkey_enc, dst, src,
					       walk.iv, nblocks);
			else
				sm4_ce_cbc_dec(ctx->rkey_dec, dst, src,
					       walk.iv, nblocks);

			kernel_neon_end();
		}

		err = skcipher_walk_done(&walk, nbytes % SM4_BLOCK_SIZE);
	}

	return err;
}

static int sm4_cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);

	return sm4_cbc_crypt(req, ctx, true);
}

static int sm4_cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);

	return sm4_cbc_crypt(req, ctx, false);
}

static int sm4_cbc_cts_crypt(struct skcipher_request *req, bool encrypt)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct scatterlist *src = req->src;
	struct scatterlist *dst = req->dst;
	struct scatterlist sg_src[2], sg_dst[2];
	struct skcipher_request subreq;
	struct skcipher_walk walk;
	int cbc_blocks;
	int err;

	if (req->cryptlen < SM4_BLOCK_SIZE)
		return -EINVAL;

	if (req->cryptlen == SM4_BLOCK_SIZE)
		return sm4_cbc_crypt(req, ctx, encrypt);

	skcipher_request_set_tfm(&subreq, tfm);
	skcipher_request_set_callback(&subreq, skcipher_request_flags(req),
				      NULL, NULL);

	/* handle the CBC cryption part */
	cbc_blocks = DIV_ROUND_UP(req->cryptlen, SM4_BLOCK_SIZE) - 2;
	if (cbc_blocks) {
		skcipher_request_set_crypt(&subreq, src, dst,
					   cbc_blocks * SM4_BLOCK_SIZE,
					   req->iv);

		err = sm4_cbc_crypt(&subreq, ctx, encrypt);
		if (err)
			return err;

		dst = src = scatterwalk_ffwd(sg_src, src, subreq.cryptlen);
		if (req->dst != req->src)
			dst = scatterwalk_ffwd(sg_dst, req->dst,
					       subreq.cryptlen);
	}

	/* handle ciphertext stealing */
	skcipher_request_set_crypt(&subreq, src, dst,
				   req->cryptlen - cbc_blocks * SM4_BLOCK_SIZE,
				   req->iv);

	err = skcipher_walk_virt(&walk, &subreq, false);
	if (err)
		return err;

	kernel_neon_begin();

	if (encrypt)
		sm4_ce_cbc_cts_enc(ctx->rkey_enc, walk.dst.virt.addr,
				   walk.src.virt.addr, walk.iv, walk.nbytes);
	else
		sm4_ce_cbc_cts_dec(ctx->rkey_dec, walk.dst.virt.addr,
				   walk.src.virt.addr, walk.iv, walk.nbytes);

	kernel_neon_end();

	return skcipher_walk_done(&walk, 0);
}

static int sm4_cbc_cts_encrypt(struct skcipher_request *req)
{
	return sm4_cbc_cts_crypt(req, true);
}

static int sm4_cbc_cts_decrypt(struct skcipher_request *req)
{
	return sm4_cbc_cts_crypt(req, false);
}

static int sm4_cfb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) > 0) {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		unsigned int nblks;

		kernel_neon_begin();

		nblks = BYTES2BLKS(nbytes);
		if (nblks) {
			sm4_ce_cfb_enc(ctx->rkey_enc, dst, src, walk.iv, nblks);
			dst += nblks * SM4_BLOCK_SIZE;
			src += nblks * SM4_BLOCK_SIZE;
			nbytes -= nblks * SM4_BLOCK_SIZE;
		}

		/* tail */
		if (walk.nbytes == walk.total && nbytes > 0) {
			u8 keystream[SM4_BLOCK_SIZE];

			sm4_ce_crypt_block(ctx->rkey_enc, keystream, walk.iv);
			crypto_xor_cpy(dst, src, keystream, nbytes);
			nbytes = 0;
		}

		kernel_neon_end();

		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int sm4_cfb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) > 0) {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		unsigned int nblks;

		kernel_neon_begin();

		nblks = BYTES2BLKS(nbytes);
		if (nblks) {
			sm4_ce_cfb_dec(ctx->rkey_enc, dst, src, walk.iv, nblks);
			dst += nblks * SM4_BLOCK_SIZE;
			src += nblks * SM4_BLOCK_SIZE;
			nbytes -= nblks * SM4_BLOCK_SIZE;
		}

		/* tail */
		if (walk.nbytes == walk.total && nbytes > 0) {
			u8 keystream[SM4_BLOCK_SIZE];

			sm4_ce_crypt_block(ctx->rkey_enc, keystream, walk.iv);
			crypto_xor_cpy(dst, src, keystream, nbytes);
			nbytes = 0;
		}

		kernel_neon_end();

		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int sm4_ctr_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) > 0) {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		unsigned int nblks;

		kernel_neon_begin();

		nblks = BYTES2BLKS(nbytes);
		if (nblks) {
			sm4_ce_ctr_enc(ctx->rkey_enc, dst, src, walk.iv, nblks);
			dst += nblks * SM4_BLOCK_SIZE;
			src += nblks * SM4_BLOCK_SIZE;
			nbytes -= nblks * SM4_BLOCK_SIZE;
		}

		/* tail */
		if (walk.nbytes == walk.total && nbytes > 0) {
			u8 keystream[SM4_BLOCK_SIZE];

			sm4_ce_crypt_block(ctx->rkey_enc, keystream, walk.iv);
			crypto_inc(walk.iv, SM4_BLOCK_SIZE);
			crypto_xor_cpy(dst, src, keystream, nbytes);
			nbytes = 0;
		}

		kernel_neon_end();

		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static struct skcipher_alg sm4_algs[] = {
	{
		.base = {
			.cra_name		= "ecb(sm4)",
			.cra_driver_name	= "ecb-sm4-ce",
			.cra_priority		= 400,
			.cra_blocksize		= SM4_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct sm4_ctx),
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= SM4_KEY_SIZE,
		.max_keysize	= SM4_KEY_SIZE,
		.setkey		= sm4_setkey,
		.encrypt	= sm4_ecb_encrypt,
		.decrypt	= sm4_ecb_decrypt,
	}, {
		.base = {
			.cra_name		= "cbc(sm4)",
			.cra_driver_name	= "cbc-sm4-ce",
			.cra_priority		= 400,
			.cra_blocksize		= SM4_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct sm4_ctx),
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= SM4_KEY_SIZE,
		.max_keysize	= SM4_KEY_SIZE,
		.ivsize		= SM4_BLOCK_SIZE,
		.setkey		= sm4_setkey,
		.encrypt	= sm4_cbc_encrypt,
		.decrypt	= sm4_cbc_decrypt,
	}, {
		.base = {
			.cra_name		= "cfb(sm4)",
			.cra_driver_name	= "cfb-sm4-ce",
			.cra_priority		= 400,
			.cra_blocksize		= 1,
			.cra_ctxsize		= sizeof(struct sm4_ctx),
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= SM4_KEY_SIZE,
		.max_keysize	= SM4_KEY_SIZE,
		.ivsize		= SM4_BLOCK_SIZE,
		.chunksize	= SM4_BLOCK_SIZE,
		.setkey		= sm4_setkey,
		.encrypt	= sm4_cfb_encrypt,
		.decrypt	= sm4_cfb_decrypt,
	}, {
		.base = {
			.cra_name		= "ctr(sm4)",
			.cra_driver_name	= "ctr-sm4-ce",
			.cra_priority		= 400,
			.cra_blocksize		= 1,
			.cra_ctxsize		= sizeof(struct sm4_ctx),
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= SM4_KEY_SIZE,
		.max_keysize	= SM4_KEY_SIZE,
		.ivsize		= SM4_BLOCK_SIZE,
		.chunksize	= SM4_BLOCK_SIZE,
		.setkey		= sm4_setkey,
		.encrypt	= sm4_ctr_crypt,
		.decrypt	= sm4_ctr_crypt,
	}, {
		.base = {
			.cra_name		= "cts(cbc(sm4))",
			.cra_driver_name	= "cts-cbc-sm4-ce",
			.cra_priority		= 400,
			.cra_blocksize		= SM4_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct sm4_ctx),
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= SM4_KEY_SIZE,
		.max_keysize	= SM4_KEY_SIZE,
		.ivsize		= SM4_BLOCK_SIZE,
		.walksize	= SM4_BLOCK_SIZE * 2,
		.setkey		= sm4_setkey,
		.encrypt	= sm4_cbc_cts_encrypt,
		.decrypt	= sm4_cbc_cts_decrypt,
	}
};

static int __init sm4_init(void)
{
	return crypto_register_skciphers(sm4_algs, ARRAY_SIZE(sm4_algs));
}

static void __exit sm4_exit(void)
{
	crypto_unregister_skciphers(sm4_algs, ARRAY_SIZE(sm4_algs));
}

module_cpu_feature_match(SM4, sm4_init);
module_exit(sm4_exit);

MODULE_DESCRIPTION("SM4 ECB/CBC/CFB/CTR using ARMv8 Crypto Extensions");
MODULE_ALIAS_CRYPTO("sm4-ce");
MODULE_ALIAS_CRYPTO("sm4");
MODULE_ALIAS_CRYPTO("ecb(sm4)");
MODULE_ALIAS_CRYPTO("cbc(sm4)");
MODULE_ALIAS_CRYPTO("cfb(sm4)");
MODULE_ALIAS_CRYPTO("ctr(sm4)");
MODULE_ALIAS_CRYPTO("cts(cbc(sm4))");
MODULE_AUTHOR("Tianjia Zhang <tianjia.zhang@linux.alibaba.com>");
MODULE_LICENSE("GPL v2");
