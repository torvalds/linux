// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm64/crypto/aes-glue.c - wrapper code for ARMv8 AES
 *
 * Copyright (C) 2013 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <asm/hwcap.h>
#include <asm/simd.h>
#include <crypto/aes.h>
#include <crypto/ctr.h>
#include <crypto/sha2.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/module.h>
#include <linux/cpufeature.h>
#include <crypto/xts.h>

#include "aes-ce-setkey.h"

#ifdef USE_V8_CRYPTO_EXTENSIONS
#define MODE			"ce"
#define PRIO			300
#define STRIDE			5
#define aes_expandkey		ce_aes_expandkey
#define aes_ecb_encrypt		ce_aes_ecb_encrypt
#define aes_ecb_decrypt		ce_aes_ecb_decrypt
#define aes_cbc_encrypt		ce_aes_cbc_encrypt
#define aes_cbc_decrypt		ce_aes_cbc_decrypt
#define aes_cbc_cts_encrypt	ce_aes_cbc_cts_encrypt
#define aes_cbc_cts_decrypt	ce_aes_cbc_cts_decrypt
#define aes_essiv_cbc_encrypt	ce_aes_essiv_cbc_encrypt
#define aes_essiv_cbc_decrypt	ce_aes_essiv_cbc_decrypt
#define aes_ctr_encrypt		ce_aes_ctr_encrypt
#define aes_xts_encrypt		ce_aes_xts_encrypt
#define aes_xts_decrypt		ce_aes_xts_decrypt
#define aes_mac_update		ce_aes_mac_update
MODULE_DESCRIPTION("AES-ECB/CBC/CTR/XTS using ARMv8 Crypto Extensions");
#else
#define MODE			"neon"
#define PRIO			200
#define STRIDE			4
#define aes_ecb_encrypt		neon_aes_ecb_encrypt
#define aes_ecb_decrypt		neon_aes_ecb_decrypt
#define aes_cbc_encrypt		neon_aes_cbc_encrypt
#define aes_cbc_decrypt		neon_aes_cbc_decrypt
#define aes_cbc_cts_encrypt	neon_aes_cbc_cts_encrypt
#define aes_cbc_cts_decrypt	neon_aes_cbc_cts_decrypt
#define aes_essiv_cbc_encrypt	neon_aes_essiv_cbc_encrypt
#define aes_essiv_cbc_decrypt	neon_aes_essiv_cbc_decrypt
#define aes_ctr_encrypt		neon_aes_ctr_encrypt
#define aes_xts_encrypt		neon_aes_xts_encrypt
#define aes_xts_decrypt		neon_aes_xts_decrypt
#define aes_mac_update		neon_aes_mac_update
MODULE_DESCRIPTION("AES-ECB/CBC/CTR/XTS using ARMv8 NEON");
#endif
#if defined(USE_V8_CRYPTO_EXTENSIONS) || !IS_ENABLED(CONFIG_CRYPTO_AES_ARM64_BS)
MODULE_ALIAS_CRYPTO("ecb(aes)");
MODULE_ALIAS_CRYPTO("cbc(aes)");
MODULE_ALIAS_CRYPTO("ctr(aes)");
MODULE_ALIAS_CRYPTO("xts(aes)");
#endif
MODULE_ALIAS_CRYPTO("cts(cbc(aes))");
MODULE_ALIAS_CRYPTO("essiv(cbc(aes),sha256)");
MODULE_ALIAS_CRYPTO("cmac(aes)");
MODULE_ALIAS_CRYPTO("xcbc(aes)");
MODULE_ALIAS_CRYPTO("cbcmac(aes)");

MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

/* defined in aes-modes.S */
asmlinkage void aes_ecb_encrypt(u8 out[], u8 const in[], u32 const rk[],
				int rounds, int blocks);
asmlinkage void aes_ecb_decrypt(u8 out[], u8 const in[], u32 const rk[],
				int rounds, int blocks);

asmlinkage void aes_cbc_encrypt(u8 out[], u8 const in[], u32 const rk[],
				int rounds, int blocks, u8 iv[]);
asmlinkage void aes_cbc_decrypt(u8 out[], u8 const in[], u32 const rk[],
				int rounds, int blocks, u8 iv[]);

asmlinkage void aes_cbc_cts_encrypt(u8 out[], u8 const in[], u32 const rk[],
				int rounds, int bytes, u8 const iv[]);
asmlinkage void aes_cbc_cts_decrypt(u8 out[], u8 const in[], u32 const rk[],
				int rounds, int bytes, u8 const iv[]);

asmlinkage void aes_ctr_encrypt(u8 out[], u8 const in[], u32 const rk[],
				int rounds, int bytes, u8 ctr[], u8 finalbuf[]);

asmlinkage void aes_xts_encrypt(u8 out[], u8 const in[], u32 const rk1[],
				int rounds, int bytes, u32 const rk2[], u8 iv[],
				int first);
asmlinkage void aes_xts_decrypt(u8 out[], u8 const in[], u32 const rk1[],
				int rounds, int bytes, u32 const rk2[], u8 iv[],
				int first);

asmlinkage void aes_essiv_cbc_encrypt(u8 out[], u8 const in[], u32 const rk1[],
				      int rounds, int blocks, u8 iv[],
				      u32 const rk2[]);
asmlinkage void aes_essiv_cbc_decrypt(u8 out[], u8 const in[], u32 const rk1[],
				      int rounds, int blocks, u8 iv[],
				      u32 const rk2[]);

asmlinkage int aes_mac_update(u8 const in[], u32 const rk[], int rounds,
			      int blocks, u8 dg[], int enc_before,
			      int enc_after);

struct crypto_aes_xts_ctx {
	struct crypto_aes_ctx key1;
	struct crypto_aes_ctx __aligned(8) key2;
};

struct crypto_aes_essiv_cbc_ctx {
	struct crypto_aes_ctx key1;
	struct crypto_aes_ctx __aligned(8) key2;
	struct crypto_shash *hash;
};

struct mac_tfm_ctx {
	struct crypto_aes_ctx key;
	u8 __aligned(8) consts[];
};

struct mac_desc_ctx {
	unsigned int len;
	u8 dg[AES_BLOCK_SIZE];
};

static int skcipher_aes_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			       unsigned int key_len)
{
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);

	return aes_expandkey(ctx, in_key, key_len);
}

static int __maybe_unused xts_set_key(struct crypto_skcipher *tfm,
				      const u8 *in_key, unsigned int key_len)
{
	struct crypto_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;

	ret = xts_verify_key(tfm, in_key, key_len);
	if (ret)
		return ret;

	ret = aes_expandkey(&ctx->key1, in_key, key_len / 2);
	if (!ret)
		ret = aes_expandkey(&ctx->key2, &in_key[key_len / 2],
				    key_len / 2);
	return ret;
}

static int __maybe_unused essiv_cbc_set_key(struct crypto_skcipher *tfm,
					    const u8 *in_key,
					    unsigned int key_len)
{
	struct crypto_aes_essiv_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	u8 digest[SHA256_DIGEST_SIZE];
	int ret;

	ret = aes_expandkey(&ctx->key1, in_key, key_len);
	if (ret)
		return ret;

	crypto_shash_tfm_digest(ctx->hash, in_key, key_len, digest);

	return aes_expandkey(&ctx->key2, digest, sizeof(digest));
}

static int __maybe_unused ecb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, rounds = 6 + ctx->key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, false);

	while ((blocks = (walk.nbytes / AES_BLOCK_SIZE))) {
		kernel_neon_begin();
		aes_ecb_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				ctx->key_enc, rounds, blocks);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	return err;
}

static int __maybe_unused ecb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, rounds = 6 + ctx->key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, false);

	while ((blocks = (walk.nbytes / AES_BLOCK_SIZE))) {
		kernel_neon_begin();
		aes_ecb_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
				ctx->key_dec, rounds, blocks);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	return err;
}

static int cbc_encrypt_walk(struct skcipher_request *req,
			    struct skcipher_walk *walk)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err = 0, rounds = 6 + ctx->key_length / 4;
	unsigned int blocks;

	while ((blocks = (walk->nbytes / AES_BLOCK_SIZE))) {
		kernel_neon_begin();
		aes_cbc_encrypt(walk->dst.virt.addr, walk->src.virt.addr,
				ctx->key_enc, rounds, blocks, walk->iv);
		kernel_neon_end();
		err = skcipher_walk_done(walk, walk->nbytes % AES_BLOCK_SIZE);
	}
	return err;
}

static int __maybe_unused cbc_encrypt(struct skcipher_request *req)
{
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		return err;
	return cbc_encrypt_walk(req, &walk);
}

static int cbc_decrypt_walk(struct skcipher_request *req,
			    struct skcipher_walk *walk)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err = 0, rounds = 6 + ctx->key_length / 4;
	unsigned int blocks;

	while ((blocks = (walk->nbytes / AES_BLOCK_SIZE))) {
		kernel_neon_begin();
		aes_cbc_decrypt(walk->dst.virt.addr, walk->src.virt.addr,
				ctx->key_dec, rounds, blocks, walk->iv);
		kernel_neon_end();
		err = skcipher_walk_done(walk, walk->nbytes % AES_BLOCK_SIZE);
	}
	return err;
}

static int __maybe_unused cbc_decrypt(struct skcipher_request *req)
{
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		return err;
	return cbc_decrypt_walk(req, &walk);
}

static int cts_cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, rounds = 6 + ctx->key_length / 4;
	int cbc_blocks = DIV_ROUND_UP(req->cryptlen, AES_BLOCK_SIZE) - 2;
	struct scatterlist *src = req->src, *dst = req->dst;
	struct scatterlist sg_src[2], sg_dst[2];
	struct skcipher_request subreq;
	struct skcipher_walk walk;

	skcipher_request_set_tfm(&subreq, tfm);
	skcipher_request_set_callback(&subreq, skcipher_request_flags(req),
				      NULL, NULL);

	if (req->cryptlen <= AES_BLOCK_SIZE) {
		if (req->cryptlen < AES_BLOCK_SIZE)
			return -EINVAL;
		cbc_blocks = 1;
	}

	if (cbc_blocks > 0) {
		skcipher_request_set_crypt(&subreq, req->src, req->dst,
					   cbc_blocks * AES_BLOCK_SIZE,
					   req->iv);

		err = skcipher_walk_virt(&walk, &subreq, false) ?:
		      cbc_encrypt_walk(&subreq, &walk);
		if (err)
			return err;

		if (req->cryptlen == AES_BLOCK_SIZE)
			return 0;

		dst = src = scatterwalk_ffwd(sg_src, req->src, subreq.cryptlen);
		if (req->dst != req->src)
			dst = scatterwalk_ffwd(sg_dst, req->dst,
					       subreq.cryptlen);
	}

	/* handle ciphertext stealing */
	skcipher_request_set_crypt(&subreq, src, dst,
				   req->cryptlen - cbc_blocks * AES_BLOCK_SIZE,
				   req->iv);

	err = skcipher_walk_virt(&walk, &subreq, false);
	if (err)
		return err;

	kernel_neon_begin();
	aes_cbc_cts_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
			    ctx->key_enc, rounds, walk.nbytes, walk.iv);
	kernel_neon_end();

	return skcipher_walk_done(&walk, 0);
}

static int cts_cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, rounds = 6 + ctx->key_length / 4;
	int cbc_blocks = DIV_ROUND_UP(req->cryptlen, AES_BLOCK_SIZE) - 2;
	struct scatterlist *src = req->src, *dst = req->dst;
	struct scatterlist sg_src[2], sg_dst[2];
	struct skcipher_request subreq;
	struct skcipher_walk walk;

	skcipher_request_set_tfm(&subreq, tfm);
	skcipher_request_set_callback(&subreq, skcipher_request_flags(req),
				      NULL, NULL);

	if (req->cryptlen <= AES_BLOCK_SIZE) {
		if (req->cryptlen < AES_BLOCK_SIZE)
			return -EINVAL;
		cbc_blocks = 1;
	}

	if (cbc_blocks > 0) {
		skcipher_request_set_crypt(&subreq, req->src, req->dst,
					   cbc_blocks * AES_BLOCK_SIZE,
					   req->iv);

		err = skcipher_walk_virt(&walk, &subreq, false) ?:
		      cbc_decrypt_walk(&subreq, &walk);
		if (err)
			return err;

		if (req->cryptlen == AES_BLOCK_SIZE)
			return 0;

		dst = src = scatterwalk_ffwd(sg_src, req->src, subreq.cryptlen);
		if (req->dst != req->src)
			dst = scatterwalk_ffwd(sg_dst, req->dst,
					       subreq.cryptlen);
	}

	/* handle ciphertext stealing */
	skcipher_request_set_crypt(&subreq, src, dst,
				   req->cryptlen - cbc_blocks * AES_BLOCK_SIZE,
				   req->iv);

	err = skcipher_walk_virt(&walk, &subreq, false);
	if (err)
		return err;

	kernel_neon_begin();
	aes_cbc_cts_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
			    ctx->key_dec, rounds, walk.nbytes, walk.iv);
	kernel_neon_end();

	return skcipher_walk_done(&walk, 0);
}

static int __maybe_unused essiv_cbc_init_tfm(struct crypto_skcipher *tfm)
{
	struct crypto_aes_essiv_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);

	ctx->hash = crypto_alloc_shash("sha256", 0, 0);

	return PTR_ERR_OR_ZERO(ctx->hash);
}

static void __maybe_unused essiv_cbc_exit_tfm(struct crypto_skcipher *tfm)
{
	struct crypto_aes_essiv_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_shash(ctx->hash);
}

static int __maybe_unused essiv_cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_essiv_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, rounds = 6 + ctx->key1.key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, false);

	blocks = walk.nbytes / AES_BLOCK_SIZE;
	if (blocks) {
		kernel_neon_begin();
		aes_essiv_cbc_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				      ctx->key1.key_enc, rounds, blocks,
				      req->iv, ctx->key2.key_enc);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	return err ?: cbc_encrypt_walk(req, &walk);
}

static int __maybe_unused essiv_cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_essiv_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, rounds = 6 + ctx->key1.key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, false);

	blocks = walk.nbytes / AES_BLOCK_SIZE;
	if (blocks) {
		kernel_neon_begin();
		aes_essiv_cbc_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
				      ctx->key1.key_dec, rounds, blocks,
				      req->iv, ctx->key2.key_enc);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	return err ?: cbc_decrypt_walk(req, &walk);
}

static int ctr_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, rounds = 6 + ctx->key_length / 4;
	struct skcipher_walk walk;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes > 0) {
		const u8 *src = walk.src.virt.addr;
		unsigned int nbytes = walk.nbytes;
		u8 *dst = walk.dst.virt.addr;
		u8 buf[AES_BLOCK_SIZE];
		unsigned int tail;

		if (unlikely(nbytes < AES_BLOCK_SIZE))
			src = memcpy(buf, src, nbytes);
		else if (nbytes < walk.total)
			nbytes &= ~(AES_BLOCK_SIZE - 1);

		kernel_neon_begin();
		aes_ctr_encrypt(dst, src, ctx->key_enc, rounds, nbytes,
				walk.iv, buf);
		kernel_neon_end();

		tail = nbytes % (STRIDE * AES_BLOCK_SIZE);
		if (tail > 0 && tail < AES_BLOCK_SIZE)
			/*
			 * The final partial block could not be returned using
			 * an overlapping store, so it was passed via buf[]
			 * instead.
			 */
			memcpy(dst + nbytes - tail, buf, tail);

		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}

	return err;
}

static void ctr_encrypt_one(struct crypto_skcipher *tfm, const u8 *src, u8 *dst)
{
	const struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	unsigned long flags;

	/*
	 * Temporarily disable interrupts to avoid races where
	 * cachelines are evicted when the CPU is interrupted
	 * to do something else.
	 */
	local_irq_save(flags);
	aes_encrypt(ctx, dst, src);
	local_irq_restore(flags);
}

static int __maybe_unused ctr_encrypt_sync(struct skcipher_request *req)
{
	if (!crypto_simd_usable())
		return crypto_ctr_encrypt_walk(req, ctr_encrypt_one);

	return ctr_encrypt(req);
}

static int __maybe_unused xts_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, first, rounds = 6 + ctx->key1.key_length / 4;
	int tail = req->cryptlen % AES_BLOCK_SIZE;
	struct scatterlist sg_src[2], sg_dst[2];
	struct skcipher_request subreq;
	struct scatterlist *src, *dst;
	struct skcipher_walk walk;

	if (req->cryptlen < AES_BLOCK_SIZE)
		return -EINVAL;

	err = skcipher_walk_virt(&walk, req, false);

	if (unlikely(tail > 0 && walk.nbytes < walk.total)) {
		int xts_blocks = DIV_ROUND_UP(req->cryptlen,
					      AES_BLOCK_SIZE) - 2;

		skcipher_walk_abort(&walk);

		skcipher_request_set_tfm(&subreq, tfm);
		skcipher_request_set_callback(&subreq,
					      skcipher_request_flags(req),
					      NULL, NULL);
		skcipher_request_set_crypt(&subreq, req->src, req->dst,
					   xts_blocks * AES_BLOCK_SIZE,
					   req->iv);
		req = &subreq;
		err = skcipher_walk_virt(&walk, req, false);
	} else {
		tail = 0;
	}

	for (first = 1; walk.nbytes >= AES_BLOCK_SIZE; first = 0) {
		int nbytes = walk.nbytes;

		if (walk.nbytes < walk.total)
			nbytes &= ~(AES_BLOCK_SIZE - 1);

		kernel_neon_begin();
		aes_xts_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				ctx->key1.key_enc, rounds, nbytes,
				ctx->key2.key_enc, walk.iv, first);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}

	if (err || likely(!tail))
		return err;

	dst = src = scatterwalk_ffwd(sg_src, req->src, req->cryptlen);
	if (req->dst != req->src)
		dst = scatterwalk_ffwd(sg_dst, req->dst, req->cryptlen);

	skcipher_request_set_crypt(req, src, dst, AES_BLOCK_SIZE + tail,
				   req->iv);

	err = skcipher_walk_virt(&walk, &subreq, false);
	if (err)
		return err;

	kernel_neon_begin();
	aes_xts_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
			ctx->key1.key_enc, rounds, walk.nbytes,
			ctx->key2.key_enc, walk.iv, first);
	kernel_neon_end();

	return skcipher_walk_done(&walk, 0);
}

static int __maybe_unused xts_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, first, rounds = 6 + ctx->key1.key_length / 4;
	int tail = req->cryptlen % AES_BLOCK_SIZE;
	struct scatterlist sg_src[2], sg_dst[2];
	struct skcipher_request subreq;
	struct scatterlist *src, *dst;
	struct skcipher_walk walk;

	if (req->cryptlen < AES_BLOCK_SIZE)
		return -EINVAL;

	err = skcipher_walk_virt(&walk, req, false);

	if (unlikely(tail > 0 && walk.nbytes < walk.total)) {
		int xts_blocks = DIV_ROUND_UP(req->cryptlen,
					      AES_BLOCK_SIZE) - 2;

		skcipher_walk_abort(&walk);

		skcipher_request_set_tfm(&subreq, tfm);
		skcipher_request_set_callback(&subreq,
					      skcipher_request_flags(req),
					      NULL, NULL);
		skcipher_request_set_crypt(&subreq, req->src, req->dst,
					   xts_blocks * AES_BLOCK_SIZE,
					   req->iv);
		req = &subreq;
		err = skcipher_walk_virt(&walk, req, false);
	} else {
		tail = 0;
	}

	for (first = 1; walk.nbytes >= AES_BLOCK_SIZE; first = 0) {
		int nbytes = walk.nbytes;

		if (walk.nbytes < walk.total)
			nbytes &= ~(AES_BLOCK_SIZE - 1);

		kernel_neon_begin();
		aes_xts_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
				ctx->key1.key_dec, rounds, nbytes,
				ctx->key2.key_enc, walk.iv, first);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
	}

	if (err || likely(!tail))
		return err;

	dst = src = scatterwalk_ffwd(sg_src, req->src, req->cryptlen);
	if (req->dst != req->src)
		dst = scatterwalk_ffwd(sg_dst, req->dst, req->cryptlen);

	skcipher_request_set_crypt(req, src, dst, AES_BLOCK_SIZE + tail,
				   req->iv);

	err = skcipher_walk_virt(&walk, &subreq, false);
	if (err)
		return err;


	kernel_neon_begin();
	aes_xts_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
			ctx->key1.key_dec, rounds, walk.nbytes,
			ctx->key2.key_enc, walk.iv, first);
	kernel_neon_end();

	return skcipher_walk_done(&walk, 0);
}

static struct skcipher_alg aes_algs[] = { {
#if defined(USE_V8_CRYPTO_EXTENSIONS) || !IS_ENABLED(CONFIG_CRYPTO_AES_ARM64_BS)
	.base = {
		.cra_name		= "__ecb(aes)",
		.cra_driver_name	= "__ecb-aes-" MODE,
		.cra_priority		= PRIO,
		.cra_flags		= CRYPTO_ALG_INTERNAL,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct crypto_aes_ctx),
		.cra_module		= THIS_MODULE,
	},
	.min_keysize	= AES_MIN_KEY_SIZE,
	.max_keysize	= AES_MAX_KEY_SIZE,
	.setkey		= skcipher_aes_setkey,
	.encrypt	= ecb_encrypt,
	.decrypt	= ecb_decrypt,
}, {
	.base = {
		.cra_name		= "__cbc(aes)",
		.cra_driver_name	= "__cbc-aes-" MODE,
		.cra_priority		= PRIO,
		.cra_flags		= CRYPTO_ALG_INTERNAL,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct crypto_aes_ctx),
		.cra_module		= THIS_MODULE,
	},
	.min_keysize	= AES_MIN_KEY_SIZE,
	.max_keysize	= AES_MAX_KEY_SIZE,
	.ivsize		= AES_BLOCK_SIZE,
	.setkey		= skcipher_aes_setkey,
	.encrypt	= cbc_encrypt,
	.decrypt	= cbc_decrypt,
}, {
	.base = {
		.cra_name		= "__ctr(aes)",
		.cra_driver_name	= "__ctr-aes-" MODE,
		.cra_priority		= PRIO,
		.cra_flags		= CRYPTO_ALG_INTERNAL,
		.cra_blocksize		= 1,
		.cra_ctxsize		= sizeof(struct crypto_aes_ctx),
		.cra_module		= THIS_MODULE,
	},
	.min_keysize	= AES_MIN_KEY_SIZE,
	.max_keysize	= AES_MAX_KEY_SIZE,
	.ivsize		= AES_BLOCK_SIZE,
	.chunksize	= AES_BLOCK_SIZE,
	.setkey		= skcipher_aes_setkey,
	.encrypt	= ctr_encrypt,
	.decrypt	= ctr_encrypt,
}, {
	.base = {
		.cra_name		= "ctr(aes)",
		.cra_driver_name	= "ctr-aes-" MODE,
		.cra_priority		= PRIO - 1,
		.cra_blocksize		= 1,
		.cra_ctxsize		= sizeof(struct crypto_aes_ctx),
		.cra_module		= THIS_MODULE,
	},
	.min_keysize	= AES_MIN_KEY_SIZE,
	.max_keysize	= AES_MAX_KEY_SIZE,
	.ivsize		= AES_BLOCK_SIZE,
	.chunksize	= AES_BLOCK_SIZE,
	.setkey		= skcipher_aes_setkey,
	.encrypt	= ctr_encrypt_sync,
	.decrypt	= ctr_encrypt_sync,
}, {
	.base = {
		.cra_name		= "__xts(aes)",
		.cra_driver_name	= "__xts-aes-" MODE,
		.cra_priority		= PRIO,
		.cra_flags		= CRYPTO_ALG_INTERNAL,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct crypto_aes_xts_ctx),
		.cra_module		= THIS_MODULE,
	},
	.min_keysize	= 2 * AES_MIN_KEY_SIZE,
	.max_keysize	= 2 * AES_MAX_KEY_SIZE,
	.ivsize		= AES_BLOCK_SIZE,
	.walksize	= 2 * AES_BLOCK_SIZE,
	.setkey		= xts_set_key,
	.encrypt	= xts_encrypt,
	.decrypt	= xts_decrypt,
}, {
#endif
	.base = {
		.cra_name		= "__cts(cbc(aes))",
		.cra_driver_name	= "__cts-cbc-aes-" MODE,
		.cra_priority		= PRIO,
		.cra_flags		= CRYPTO_ALG_INTERNAL,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct crypto_aes_ctx),
		.cra_module		= THIS_MODULE,
	},
	.min_keysize	= AES_MIN_KEY_SIZE,
	.max_keysize	= AES_MAX_KEY_SIZE,
	.ivsize		= AES_BLOCK_SIZE,
	.walksize	= 2 * AES_BLOCK_SIZE,
	.setkey		= skcipher_aes_setkey,
	.encrypt	= cts_cbc_encrypt,
	.decrypt	= cts_cbc_decrypt,
}, {
	.base = {
		.cra_name		= "__essiv(cbc(aes),sha256)",
		.cra_driver_name	= "__essiv-cbc-aes-sha256-" MODE,
		.cra_priority		= PRIO + 1,
		.cra_flags		= CRYPTO_ALG_INTERNAL,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct crypto_aes_essiv_cbc_ctx),
		.cra_module		= THIS_MODULE,
	},
	.min_keysize	= AES_MIN_KEY_SIZE,
	.max_keysize	= AES_MAX_KEY_SIZE,
	.ivsize		= AES_BLOCK_SIZE,
	.setkey		= essiv_cbc_set_key,
	.encrypt	= essiv_cbc_encrypt,
	.decrypt	= essiv_cbc_decrypt,
	.init		= essiv_cbc_init_tfm,
	.exit		= essiv_cbc_exit_tfm,
} };

static int cbcmac_setkey(struct crypto_shash *tfm, const u8 *in_key,
			 unsigned int key_len)
{
	struct mac_tfm_ctx *ctx = crypto_shash_ctx(tfm);

	return aes_expandkey(&ctx->key, in_key, key_len);
}

static void cmac_gf128_mul_by_x(be128 *y, const be128 *x)
{
	u64 a = be64_to_cpu(x->a);
	u64 b = be64_to_cpu(x->b);

	y->a = cpu_to_be64((a << 1) | (b >> 63));
	y->b = cpu_to_be64((b << 1) ^ ((a >> 63) ? 0x87 : 0));
}

static int cmac_setkey(struct crypto_shash *tfm, const u8 *in_key,
		       unsigned int key_len)
{
	struct mac_tfm_ctx *ctx = crypto_shash_ctx(tfm);
	be128 *consts = (be128 *)ctx->consts;
	int rounds = 6 + key_len / 4;
	int err;

	err = cbcmac_setkey(tfm, in_key, key_len);
	if (err)
		return err;

	/* encrypt the zero vector */
	kernel_neon_begin();
	aes_ecb_encrypt(ctx->consts, (u8[AES_BLOCK_SIZE]){}, ctx->key.key_enc,
			rounds, 1);
	kernel_neon_end();

	cmac_gf128_mul_by_x(consts, consts);
	cmac_gf128_mul_by_x(consts + 1, consts);

	return 0;
}

static int xcbc_setkey(struct crypto_shash *tfm, const u8 *in_key,
		       unsigned int key_len)
{
	static u8 const ks[3][AES_BLOCK_SIZE] = {
		{ [0 ... AES_BLOCK_SIZE - 1] = 0x1 },
		{ [0 ... AES_BLOCK_SIZE - 1] = 0x2 },
		{ [0 ... AES_BLOCK_SIZE - 1] = 0x3 },
	};

	struct mac_tfm_ctx *ctx = crypto_shash_ctx(tfm);
	int rounds = 6 + key_len / 4;
	u8 key[AES_BLOCK_SIZE];
	int err;

	err = cbcmac_setkey(tfm, in_key, key_len);
	if (err)
		return err;

	kernel_neon_begin();
	aes_ecb_encrypt(key, ks[0], ctx->key.key_enc, rounds, 1);
	aes_ecb_encrypt(ctx->consts, ks[1], ctx->key.key_enc, rounds, 2);
	kernel_neon_end();

	return cbcmac_setkey(tfm, key, sizeof(key));
}

static int mac_init(struct shash_desc *desc)
{
	struct mac_desc_ctx *ctx = shash_desc_ctx(desc);

	memset(ctx->dg, 0, AES_BLOCK_SIZE);
	ctx->len = 0;

	return 0;
}

static void mac_do_update(struct crypto_aes_ctx *ctx, u8 const in[], int blocks,
			  u8 dg[], int enc_before, int enc_after)
{
	int rounds = 6 + ctx->key_length / 4;

	if (crypto_simd_usable()) {
		int rem;

		do {
			kernel_neon_begin();
			rem = aes_mac_update(in, ctx->key_enc, rounds, blocks,
					     dg, enc_before, enc_after);
			kernel_neon_end();
			in += (blocks - rem) * AES_BLOCK_SIZE;
			blocks = rem;
			enc_before = 0;
		} while (blocks);
	} else {
		if (enc_before)
			aes_encrypt(ctx, dg, dg);

		while (blocks--) {
			crypto_xor(dg, in, AES_BLOCK_SIZE);
			in += AES_BLOCK_SIZE;

			if (blocks || enc_after)
				aes_encrypt(ctx, dg, dg);
		}
	}
}

static int mac_update(struct shash_desc *desc, const u8 *p, unsigned int len)
{
	struct mac_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct mac_desc_ctx *ctx = shash_desc_ctx(desc);

	while (len > 0) {
		unsigned int l;

		if ((ctx->len % AES_BLOCK_SIZE) == 0 &&
		    (ctx->len + len) > AES_BLOCK_SIZE) {

			int blocks = len / AES_BLOCK_SIZE;

			len %= AES_BLOCK_SIZE;

			mac_do_update(&tctx->key, p, blocks, ctx->dg,
				      (ctx->len != 0), (len != 0));

			p += blocks * AES_BLOCK_SIZE;

			if (!len) {
				ctx->len = AES_BLOCK_SIZE;
				break;
			}
			ctx->len = 0;
		}

		l = min(len, AES_BLOCK_SIZE - ctx->len);

		if (l <= AES_BLOCK_SIZE) {
			crypto_xor(ctx->dg + ctx->len, p, l);
			ctx->len += l;
			len -= l;
			p += l;
		}
	}

	return 0;
}

static int cbcmac_final(struct shash_desc *desc, u8 *out)
{
	struct mac_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct mac_desc_ctx *ctx = shash_desc_ctx(desc);

	mac_do_update(&tctx->key, NULL, 0, ctx->dg, (ctx->len != 0), 0);

	memcpy(out, ctx->dg, AES_BLOCK_SIZE);

	return 0;
}

static int cmac_final(struct shash_desc *desc, u8 *out)
{
	struct mac_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct mac_desc_ctx *ctx = shash_desc_ctx(desc);
	u8 *consts = tctx->consts;

	if (ctx->len != AES_BLOCK_SIZE) {
		ctx->dg[ctx->len] ^= 0x80;
		consts += AES_BLOCK_SIZE;
	}

	mac_do_update(&tctx->key, consts, 1, ctx->dg, 0, 1);

	memcpy(out, ctx->dg, AES_BLOCK_SIZE);

	return 0;
}

static struct shash_alg mac_algs[] = { {
	.base.cra_name		= "cmac(aes)",
	.base.cra_driver_name	= "cmac-aes-" MODE,
	.base.cra_priority	= PRIO,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct mac_tfm_ctx) +
				  2 * AES_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,

	.digestsize		= AES_BLOCK_SIZE,
	.init			= mac_init,
	.update			= mac_update,
	.final			= cmac_final,
	.setkey			= cmac_setkey,
	.descsize		= sizeof(struct mac_desc_ctx),
}, {
	.base.cra_name		= "xcbc(aes)",
	.base.cra_driver_name	= "xcbc-aes-" MODE,
	.base.cra_priority	= PRIO,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct mac_tfm_ctx) +
				  2 * AES_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,

	.digestsize		= AES_BLOCK_SIZE,
	.init			= mac_init,
	.update			= mac_update,
	.final			= cmac_final,
	.setkey			= xcbc_setkey,
	.descsize		= sizeof(struct mac_desc_ctx),
}, {
	.base.cra_name		= "cbcmac(aes)",
	.base.cra_driver_name	= "cbcmac-aes-" MODE,
	.base.cra_priority	= PRIO,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct mac_tfm_ctx),
	.base.cra_module	= THIS_MODULE,

	.digestsize		= AES_BLOCK_SIZE,
	.init			= mac_init,
	.update			= mac_update,
	.final			= cbcmac_final,
	.setkey			= cbcmac_setkey,
	.descsize		= sizeof(struct mac_desc_ctx),
} };

static struct simd_skcipher_alg *aes_simd_algs[ARRAY_SIZE(aes_algs)];

static void aes_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(aes_simd_algs); i++)
		if (aes_simd_algs[i])
			simd_skcipher_free(aes_simd_algs[i]);

	crypto_unregister_shashes(mac_algs, ARRAY_SIZE(mac_algs));
	crypto_unregister_skciphers(aes_algs, ARRAY_SIZE(aes_algs));
}

static int __init aes_init(void)
{
	struct simd_skcipher_alg *simd;
	const char *basename;
	const char *algname;
	const char *drvname;
	int err;
	int i;

	err = crypto_register_skciphers(aes_algs, ARRAY_SIZE(aes_algs));
	if (err)
		return err;

	err = crypto_register_shashes(mac_algs, ARRAY_SIZE(mac_algs));
	if (err)
		goto unregister_ciphers;

	for (i = 0; i < ARRAY_SIZE(aes_algs); i++) {
		if (!(aes_algs[i].base.cra_flags & CRYPTO_ALG_INTERNAL))
			continue;

		algname = aes_algs[i].base.cra_name + 2;
		drvname = aes_algs[i].base.cra_driver_name + 2;
		basename = aes_algs[i].base.cra_driver_name;
		simd = simd_skcipher_create_compat(algname, drvname, basename);
		err = PTR_ERR(simd);
		if (IS_ERR(simd))
			goto unregister_simds;

		aes_simd_algs[i] = simd;
	}

	return 0;

unregister_simds:
	aes_exit();
	return err;
unregister_ciphers:
	crypto_unregister_skciphers(aes_algs, ARRAY_SIZE(aes_algs));
	return err;
}

#ifdef USE_V8_CRYPTO_EXTENSIONS
module_cpu_feature_match(AES, aes_init);
#else
module_init(aes_init);
EXPORT_SYMBOL(neon_aes_ecb_encrypt);
EXPORT_SYMBOL(neon_aes_cbc_encrypt);
EXPORT_SYMBOL(neon_aes_xts_encrypt);
EXPORT_SYMBOL(neon_aes_xts_decrypt);
#endif
module_exit(aes_exit);
