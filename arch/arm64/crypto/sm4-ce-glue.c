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
#include <crypto/b128ops.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <crypto/internal/hash.h>
#include <crypto/scatterwalk.h>
#include <crypto/xts.h>
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
asmlinkage void sm4_ce_xts_enc(const u32 *rkey1, u8 *dst, const u8 *src,
			       u8 *tweak, unsigned int nbytes,
			       const u32 *rkey2_enc);
asmlinkage void sm4_ce_xts_dec(const u32 *rkey1, u8 *dst, const u8 *src,
			       u8 *tweak, unsigned int nbytes,
			       const u32 *rkey2_enc);
asmlinkage void sm4_ce_mac_update(const u32 *rkey_enc, u8 *digest,
				  const u8 *src, unsigned int nblocks,
				  bool enc_before, bool enc_after);

EXPORT_SYMBOL(sm4_ce_expand_key);
EXPORT_SYMBOL(sm4_ce_crypt_block);
EXPORT_SYMBOL(sm4_ce_cbc_enc);
EXPORT_SYMBOL(sm4_ce_cfb_enc);

struct sm4_xts_ctx {
	struct sm4_ctx key1;
	struct sm4_ctx key2;
};

struct sm4_mac_tfm_ctx {
	struct sm4_ctx key;
	u8 __aligned(8) consts[];
};

struct sm4_mac_desc_ctx {
	unsigned int len;
	u8 digest[SM4_BLOCK_SIZE];
};

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

static int sm4_xts_setkey(struct crypto_skcipher *tfm, const u8 *key,
			  unsigned int key_len)
{
	struct sm4_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;

	if (key_len != SM4_KEY_SIZE * 2)
		return -EINVAL;

	ret = xts_verify_key(tfm, key, key_len);
	if (ret)
		return ret;

	kernel_neon_begin();
	sm4_ce_expand_key(key, ctx->key1.rkey_enc,
			  ctx->key1.rkey_dec, crypto_sm4_fk, crypto_sm4_ck);
	sm4_ce_expand_key(&key[SM4_KEY_SIZE], ctx->key2.rkey_enc,
			  ctx->key2.rkey_dec, crypto_sm4_fk, crypto_sm4_ck);
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

static int sm4_xts_crypt(struct skcipher_request *req, bool encrypt)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int tail = req->cryptlen % SM4_BLOCK_SIZE;
	const u32 *rkey2_enc = ctx->key2.rkey_enc;
	struct scatterlist sg_src[2], sg_dst[2];
	struct skcipher_request subreq;
	struct scatterlist *src, *dst;
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	if (req->cryptlen < SM4_BLOCK_SIZE)
		return -EINVAL;

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		return err;

	if (unlikely(tail > 0 && walk.nbytes < walk.total)) {
		int nblocks = DIV_ROUND_UP(req->cryptlen, SM4_BLOCK_SIZE) - 2;

		skcipher_walk_abort(&walk);

		skcipher_request_set_tfm(&subreq, tfm);
		skcipher_request_set_callback(&subreq,
					      skcipher_request_flags(req),
					      NULL, NULL);
		skcipher_request_set_crypt(&subreq, req->src, req->dst,
					   nblocks * SM4_BLOCK_SIZE, req->iv);

		err = skcipher_walk_virt(&walk, &subreq, false);
		if (err)
			return err;
	} else {
		tail = 0;
	}

	while ((nbytes = walk.nbytes) >= SM4_BLOCK_SIZE) {
		if (nbytes < walk.total)
			nbytes &= ~(SM4_BLOCK_SIZE - 1);

		kernel_neon_begin();

		if (encrypt)
			sm4_ce_xts_enc(ctx->key1.rkey_enc, walk.dst.virt.addr,
				       walk.src.virt.addr, walk.iv, nbytes,
				       rkey2_enc);
		else
			sm4_ce_xts_dec(ctx->key1.rkey_dec, walk.dst.virt.addr,
				       walk.src.virt.addr, walk.iv, nbytes,
				       rkey2_enc);

		kernel_neon_end();

		rkey2_enc = NULL;

		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
		if (err)
			return err;
	}

	if (likely(tail == 0))
		return 0;

	/* handle ciphertext stealing */

	dst = src = scatterwalk_ffwd(sg_src, req->src, subreq.cryptlen);
	if (req->dst != req->src)
		dst = scatterwalk_ffwd(sg_dst, req->dst, subreq.cryptlen);

	skcipher_request_set_crypt(&subreq, src, dst, SM4_BLOCK_SIZE + tail,
				   req->iv);

	err = skcipher_walk_virt(&walk, &subreq, false);
	if (err)
		return err;

	kernel_neon_begin();

	if (encrypt)
		sm4_ce_xts_enc(ctx->key1.rkey_enc, walk.dst.virt.addr,
			       walk.src.virt.addr, walk.iv, walk.nbytes,
			       rkey2_enc);
	else
		sm4_ce_xts_dec(ctx->key1.rkey_dec, walk.dst.virt.addr,
			       walk.src.virt.addr, walk.iv, walk.nbytes,
			       rkey2_enc);

	kernel_neon_end();

	return skcipher_walk_done(&walk, 0);
}

static int sm4_xts_encrypt(struct skcipher_request *req)
{
	return sm4_xts_crypt(req, true);
}

static int sm4_xts_decrypt(struct skcipher_request *req)
{
	return sm4_xts_crypt(req, false);
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
	}, {
		.base = {
			.cra_name		= "xts(sm4)",
			.cra_driver_name	= "xts-sm4-ce",
			.cra_priority		= 400,
			.cra_blocksize		= SM4_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct sm4_xts_ctx),
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= SM4_KEY_SIZE * 2,
		.max_keysize	= SM4_KEY_SIZE * 2,
		.ivsize		= SM4_BLOCK_SIZE,
		.walksize	= SM4_BLOCK_SIZE * 2,
		.setkey		= sm4_xts_setkey,
		.encrypt	= sm4_xts_encrypt,
		.decrypt	= sm4_xts_decrypt,
	}
};

static int sm4_cbcmac_setkey(struct crypto_shash *tfm, const u8 *key,
			     unsigned int key_len)
{
	struct sm4_mac_tfm_ctx *ctx = crypto_shash_ctx(tfm);

	if (key_len != SM4_KEY_SIZE)
		return -EINVAL;

	kernel_neon_begin();
	sm4_ce_expand_key(key, ctx->key.rkey_enc, ctx->key.rkey_dec,
			  crypto_sm4_fk, crypto_sm4_ck);
	kernel_neon_end();

	return 0;
}

static int sm4_cmac_setkey(struct crypto_shash *tfm, const u8 *key,
			   unsigned int key_len)
{
	struct sm4_mac_tfm_ctx *ctx = crypto_shash_ctx(tfm);
	be128 *consts = (be128 *)ctx->consts;
	u64 a, b;

	if (key_len != SM4_KEY_SIZE)
		return -EINVAL;

	memset(consts, 0, SM4_BLOCK_SIZE);

	kernel_neon_begin();

	sm4_ce_expand_key(key, ctx->key.rkey_enc, ctx->key.rkey_dec,
			  crypto_sm4_fk, crypto_sm4_ck);

	/* encrypt the zero block */
	sm4_ce_crypt_block(ctx->key.rkey_enc, (u8 *)consts, (const u8 *)consts);

	kernel_neon_end();

	/* gf(2^128) multiply zero-ciphertext with u and u^2 */
	a = be64_to_cpu(consts[0].a);
	b = be64_to_cpu(consts[0].b);
	consts[0].a = cpu_to_be64((a << 1) | (b >> 63));
	consts[0].b = cpu_to_be64((b << 1) ^ ((a >> 63) ? 0x87 : 0));

	a = be64_to_cpu(consts[0].a);
	b = be64_to_cpu(consts[0].b);
	consts[1].a = cpu_to_be64((a << 1) | (b >> 63));
	consts[1].b = cpu_to_be64((b << 1) ^ ((a >> 63) ? 0x87 : 0));

	return 0;
}

static int sm4_xcbc_setkey(struct crypto_shash *tfm, const u8 *key,
			   unsigned int key_len)
{
	struct sm4_mac_tfm_ctx *ctx = crypto_shash_ctx(tfm);
	u8 __aligned(8) key2[SM4_BLOCK_SIZE];
	static u8 const ks[3][SM4_BLOCK_SIZE] = {
		{ [0 ... SM4_BLOCK_SIZE - 1] = 0x1},
		{ [0 ... SM4_BLOCK_SIZE - 1] = 0x2},
		{ [0 ... SM4_BLOCK_SIZE - 1] = 0x3},
	};

	if (key_len != SM4_KEY_SIZE)
		return -EINVAL;

	kernel_neon_begin();

	sm4_ce_expand_key(key, ctx->key.rkey_enc, ctx->key.rkey_dec,
			  crypto_sm4_fk, crypto_sm4_ck);

	sm4_ce_crypt_block(ctx->key.rkey_enc, key2, ks[0]);
	sm4_ce_crypt(ctx->key.rkey_enc, ctx->consts, ks[1], 2);

	sm4_ce_expand_key(key2, ctx->key.rkey_enc, ctx->key.rkey_dec,
			  crypto_sm4_fk, crypto_sm4_ck);

	kernel_neon_end();

	return 0;
}

static int sm4_mac_init(struct shash_desc *desc)
{
	struct sm4_mac_desc_ctx *ctx = shash_desc_ctx(desc);

	memset(ctx->digest, 0, SM4_BLOCK_SIZE);
	ctx->len = 0;

	return 0;
}

static int sm4_mac_update(struct shash_desc *desc, const u8 *p,
			  unsigned int len)
{
	struct sm4_mac_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct sm4_mac_desc_ctx *ctx = shash_desc_ctx(desc);
	unsigned int l, nblocks;

	if (len == 0)
		return 0;

	if (ctx->len || ctx->len + len < SM4_BLOCK_SIZE) {
		l = min(len, SM4_BLOCK_SIZE - ctx->len);

		crypto_xor(ctx->digest + ctx->len, p, l);
		ctx->len += l;
		len -= l;
		p += l;
	}

	if (len && (ctx->len % SM4_BLOCK_SIZE) == 0) {
		kernel_neon_begin();

		if (len < SM4_BLOCK_SIZE && ctx->len == SM4_BLOCK_SIZE) {
			sm4_ce_crypt_block(tctx->key.rkey_enc,
					   ctx->digest, ctx->digest);
			ctx->len = 0;
		} else {
			nblocks = len / SM4_BLOCK_SIZE;
			len %= SM4_BLOCK_SIZE;

			sm4_ce_mac_update(tctx->key.rkey_enc, ctx->digest, p,
					  nblocks, (ctx->len == SM4_BLOCK_SIZE),
					  (len != 0));

			p += nblocks * SM4_BLOCK_SIZE;

			if (len == 0)
				ctx->len = SM4_BLOCK_SIZE;
		}

		kernel_neon_end();

		if (len) {
			crypto_xor(ctx->digest, p, len);
			ctx->len = len;
		}
	}

	return 0;
}

static int sm4_cmac_final(struct shash_desc *desc, u8 *out)
{
	struct sm4_mac_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct sm4_mac_desc_ctx *ctx = shash_desc_ctx(desc);
	const u8 *consts = tctx->consts;

	if (ctx->len != SM4_BLOCK_SIZE) {
		ctx->digest[ctx->len] ^= 0x80;
		consts += SM4_BLOCK_SIZE;
	}

	kernel_neon_begin();
	sm4_ce_mac_update(tctx->key.rkey_enc, ctx->digest, consts, 1,
			  false, true);
	kernel_neon_end();

	memcpy(out, ctx->digest, SM4_BLOCK_SIZE);

	return 0;
}

static int sm4_cbcmac_final(struct shash_desc *desc, u8 *out)
{
	struct sm4_mac_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct sm4_mac_desc_ctx *ctx = shash_desc_ctx(desc);

	if (ctx->len) {
		kernel_neon_begin();
		sm4_ce_crypt_block(tctx->key.rkey_enc, ctx->digest,
				   ctx->digest);
		kernel_neon_end();
	}

	memcpy(out, ctx->digest, SM4_BLOCK_SIZE);

	return 0;
}

static struct shash_alg sm4_mac_algs[] = {
	{
		.base = {
			.cra_name		= "cmac(sm4)",
			.cra_driver_name	= "cmac-sm4-ce",
			.cra_priority		= 400,
			.cra_blocksize		= SM4_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct sm4_mac_tfm_ctx)
							+ SM4_BLOCK_SIZE * 2,
			.cra_module		= THIS_MODULE,
		},
		.digestsize	= SM4_BLOCK_SIZE,
		.init		= sm4_mac_init,
		.update		= sm4_mac_update,
		.final		= sm4_cmac_final,
		.setkey		= sm4_cmac_setkey,
		.descsize	= sizeof(struct sm4_mac_desc_ctx),
	}, {
		.base = {
			.cra_name		= "xcbc(sm4)",
			.cra_driver_name	= "xcbc-sm4-ce",
			.cra_priority		= 400,
			.cra_blocksize		= SM4_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct sm4_mac_tfm_ctx)
							+ SM4_BLOCK_SIZE * 2,
			.cra_module		= THIS_MODULE,
		},
		.digestsize	= SM4_BLOCK_SIZE,
		.init		= sm4_mac_init,
		.update		= sm4_mac_update,
		.final		= sm4_cmac_final,
		.setkey		= sm4_xcbc_setkey,
		.descsize	= sizeof(struct sm4_mac_desc_ctx),
	}, {
		.base = {
			.cra_name		= "cbcmac(sm4)",
			.cra_driver_name	= "cbcmac-sm4-ce",
			.cra_priority		= 400,
			.cra_blocksize		= 1,
			.cra_ctxsize		= sizeof(struct sm4_mac_tfm_ctx),
			.cra_module		= THIS_MODULE,
		},
		.digestsize	= SM4_BLOCK_SIZE,
		.init		= sm4_mac_init,
		.update		= sm4_mac_update,
		.final		= sm4_cbcmac_final,
		.setkey		= sm4_cbcmac_setkey,
		.descsize	= sizeof(struct sm4_mac_desc_ctx),
	}
};

static int __init sm4_init(void)
{
	int err;

	err = crypto_register_skciphers(sm4_algs, ARRAY_SIZE(sm4_algs));
	if (err)
		return err;

	err = crypto_register_shashes(sm4_mac_algs, ARRAY_SIZE(sm4_mac_algs));
	if (err)
		goto out_err;

	return 0;

out_err:
	crypto_unregister_skciphers(sm4_algs, ARRAY_SIZE(sm4_algs));
	return err;
}

static void __exit sm4_exit(void)
{
	crypto_unregister_shashes(sm4_mac_algs, ARRAY_SIZE(sm4_mac_algs));
	crypto_unregister_skciphers(sm4_algs, ARRAY_SIZE(sm4_algs));
}

module_cpu_feature_match(SM4, sm4_init);
module_exit(sm4_exit);

MODULE_DESCRIPTION("SM4 ECB/CBC/CFB/CTR/XTS using ARMv8 Crypto Extensions");
MODULE_ALIAS_CRYPTO("sm4-ce");
MODULE_ALIAS_CRYPTO("sm4");
MODULE_ALIAS_CRYPTO("ecb(sm4)");
MODULE_ALIAS_CRYPTO("cbc(sm4)");
MODULE_ALIAS_CRYPTO("cfb(sm4)");
MODULE_ALIAS_CRYPTO("ctr(sm4)");
MODULE_ALIAS_CRYPTO("cts(cbc(sm4))");
MODULE_ALIAS_CRYPTO("xts(sm4)");
MODULE_ALIAS_CRYPTO("cmac(sm4)");
MODULE_ALIAS_CRYPTO("xcbc(sm4)");
MODULE_ALIAS_CRYPTO("cbcmac(sm4)");
MODULE_AUTHOR("Tianjia Zhang <tianjia.zhang@linux.alibaba.com>");
MODULE_LICENSE("GPL v2");
