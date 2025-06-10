// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bit sliced AES using NEON instructions
 *
 * Copyright (C) 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <asm/simd.h>
#include <crypto/aes.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <crypto/xts.h>
#include <linux/module.h>
#include "aes-cipher.h"

MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_DESCRIPTION("Bit sliced AES using NEON instructions");
MODULE_LICENSE("GPL v2");

MODULE_ALIAS_CRYPTO("ecb(aes)");
MODULE_ALIAS_CRYPTO("cbc(aes)");
MODULE_ALIAS_CRYPTO("ctr(aes)");
MODULE_ALIAS_CRYPTO("xts(aes)");

asmlinkage void aesbs_convert_key(u8 out[], u32 const rk[], int rounds);

asmlinkage void aesbs_ecb_encrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks);
asmlinkage void aesbs_ecb_decrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks);

asmlinkage void aesbs_cbc_decrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[]);

asmlinkage void aesbs_ctr_encrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 ctr[]);

asmlinkage void aesbs_xts_encrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[], int);
asmlinkage void aesbs_xts_decrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[], int);

struct aesbs_ctx {
	int	rounds;
	u8	rk[13 * (8 * AES_BLOCK_SIZE) + 32] __aligned(AES_BLOCK_SIZE);
};

struct aesbs_cbc_ctx {
	struct aesbs_ctx	key;
	struct crypto_aes_ctx	fallback;
};

struct aesbs_xts_ctx {
	struct aesbs_ctx	key;
	struct crypto_aes_ctx	fallback;
	struct crypto_aes_ctx	tweak_key;
};

static int aesbs_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			unsigned int key_len)
{
	struct aesbs_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_aes_ctx rk;
	int err;

	err = aes_expandkey(&rk, in_key, key_len);
	if (err)
		return err;

	ctx->rounds = 6 + key_len / 4;

	kernel_neon_begin();
	aesbs_convert_key(ctx->rk, rk.key_enc, ctx->rounds);
	kernel_neon_end();

	return 0;
}

static int __ecb_crypt(struct skcipher_request *req,
		       void (*fn)(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks))
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes >= AES_BLOCK_SIZE) {
		unsigned int blocks = walk.nbytes / AES_BLOCK_SIZE;

		if (walk.nbytes < walk.total)
			blocks = round_down(blocks,
					    walk.stride / AES_BLOCK_SIZE);

		kernel_neon_begin();
		fn(walk.dst.virt.addr, walk.src.virt.addr, ctx->rk,
		   ctx->rounds, blocks);
		kernel_neon_end();
		err = skcipher_walk_done(&walk,
					 walk.nbytes - blocks * AES_BLOCK_SIZE);
	}

	return err;
}

static int ecb_encrypt(struct skcipher_request *req)
{
	return __ecb_crypt(req, aesbs_ecb_encrypt);
}

static int ecb_decrypt(struct skcipher_request *req)
{
	return __ecb_crypt(req, aesbs_ecb_decrypt);
}

static int aesbs_cbc_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			    unsigned int key_len)
{
	struct aesbs_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err;

	err = aes_expandkey(&ctx->fallback, in_key, key_len);
	if (err)
		return err;

	ctx->key.rounds = 6 + key_len / 4;

	kernel_neon_begin();
	aesbs_convert_key(ctx->key.rk, ctx->fallback.key_enc, ctx->key.rounds);
	kernel_neon_end();

	return 0;
}

static int cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct aesbs_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) >= AES_BLOCK_SIZE) {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		u8 *prev = walk.iv;

		do {
			crypto_xor_cpy(dst, src, prev, AES_BLOCK_SIZE);
			__aes_arm_encrypt(ctx->fallback.key_enc,
					  ctx->key.rounds, dst, dst);
			prev = dst;
			src += AES_BLOCK_SIZE;
			dst += AES_BLOCK_SIZE;
			nbytes -= AES_BLOCK_SIZE;
		} while (nbytes >= AES_BLOCK_SIZE);
		memcpy(walk.iv, prev, AES_BLOCK_SIZE);
		err = skcipher_walk_done(&walk, nbytes);
	}
	return err;
}

static int cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes >= AES_BLOCK_SIZE) {
		unsigned int blocks = walk.nbytes / AES_BLOCK_SIZE;

		if (walk.nbytes < walk.total)
			blocks = round_down(blocks,
					    walk.stride / AES_BLOCK_SIZE);

		kernel_neon_begin();
		aesbs_cbc_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
				  ctx->key.rk, ctx->key.rounds, blocks,
				  walk.iv);
		kernel_neon_end();
		err = skcipher_walk_done(&walk,
					 walk.nbytes - blocks * AES_BLOCK_SIZE);
	}

	return err;
}

static int ctr_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	u8 buf[AES_BLOCK_SIZE];
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes > 0) {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		unsigned int bytes = walk.nbytes;

		if (unlikely(bytes < AES_BLOCK_SIZE))
			src = dst = memcpy(buf + sizeof(buf) - bytes,
					   src, bytes);
		else if (walk.nbytes < walk.total)
			bytes &= ~(8 * AES_BLOCK_SIZE - 1);

		kernel_neon_begin();
		aesbs_ctr_encrypt(dst, src, ctx->rk, ctx->rounds, bytes, walk.iv);
		kernel_neon_end();

		if (unlikely(bytes < AES_BLOCK_SIZE))
			memcpy(walk.dst.virt.addr,
			       buf + sizeof(buf) - bytes, bytes);

		err = skcipher_walk_done(&walk, walk.nbytes - bytes);
	}

	return err;
}

static int aesbs_xts_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			    unsigned int key_len)
{
	struct aesbs_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err;

	err = xts_verify_key(tfm, in_key, key_len);
	if (err)
		return err;

	key_len /= 2;
	err = aes_expandkey(&ctx->fallback, in_key, key_len);
	if (err)
		return err;
	err = aes_expandkey(&ctx->tweak_key, in_key + key_len, key_len);
	if (err)
		return err;

	return aesbs_setkey(tfm, in_key, key_len);
}

static int __xts_crypt(struct skcipher_request *req, bool encrypt,
		       void (*fn)(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[], int))
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	const int rounds = ctx->key.rounds;
	int tail = req->cryptlen % AES_BLOCK_SIZE;
	struct skcipher_request subreq;
	u8 buf[2 * AES_BLOCK_SIZE];
	struct skcipher_walk walk;
	int err;

	if (req->cryptlen < AES_BLOCK_SIZE)
		return -EINVAL;

	if (unlikely(tail)) {
		skcipher_request_set_tfm(&subreq, tfm);
		skcipher_request_set_callback(&subreq,
					      skcipher_request_flags(req),
					      NULL, NULL);
		skcipher_request_set_crypt(&subreq, req->src, req->dst,
					   req->cryptlen - tail, req->iv);
		req = &subreq;
	}

	err = skcipher_walk_virt(&walk, req, true);
	if (err)
		return err;

	__aes_arm_encrypt(ctx->tweak_key.key_enc, rounds, walk.iv, walk.iv);

	while (walk.nbytes >= AES_BLOCK_SIZE) {
		unsigned int blocks = walk.nbytes / AES_BLOCK_SIZE;
		int reorder_last_tweak = !encrypt && tail > 0;

		if (walk.nbytes < walk.total) {
			blocks = round_down(blocks,
					    walk.stride / AES_BLOCK_SIZE);
			reorder_last_tweak = 0;
		}

		kernel_neon_begin();
		fn(walk.dst.virt.addr, walk.src.virt.addr, ctx->key.rk,
		   rounds, blocks, walk.iv, reorder_last_tweak);
		kernel_neon_end();
		err = skcipher_walk_done(&walk,
					 walk.nbytes - blocks * AES_BLOCK_SIZE);
	}

	if (err || likely(!tail))
		return err;

	/* handle ciphertext stealing */
	scatterwalk_map_and_copy(buf, req->dst, req->cryptlen - AES_BLOCK_SIZE,
				 AES_BLOCK_SIZE, 0);
	memcpy(buf + AES_BLOCK_SIZE, buf, tail);
	scatterwalk_map_and_copy(buf, req->src, req->cryptlen, tail, 0);

	crypto_xor(buf, req->iv, AES_BLOCK_SIZE);

	if (encrypt)
		__aes_arm_encrypt(ctx->fallback.key_enc, rounds, buf, buf);
	else
		__aes_arm_decrypt(ctx->fallback.key_dec, rounds, buf, buf);

	crypto_xor(buf, req->iv, AES_BLOCK_SIZE);

	scatterwalk_map_and_copy(buf, req->dst, req->cryptlen - AES_BLOCK_SIZE,
				 AES_BLOCK_SIZE + tail, 1);
	return 0;
}

static int xts_encrypt(struct skcipher_request *req)
{
	return __xts_crypt(req, true, aesbs_xts_encrypt);
}

static int xts_decrypt(struct skcipher_request *req)
{
	return __xts_crypt(req, false, aesbs_xts_decrypt);
}

static struct skcipher_alg aes_algs[] = { {
	.base.cra_name		= "ecb(aes)",
	.base.cra_driver_name	= "ecb-aes-neonbs",
	.base.cra_priority	= 250,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct aesbs_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.setkey			= aesbs_setkey,
	.encrypt		= ecb_encrypt,
	.decrypt		= ecb_decrypt,
}, {
	.base.cra_name		= "cbc(aes)",
	.base.cra_driver_name	= "cbc-aes-neonbs",
	.base.cra_priority	= 250,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct aesbs_cbc_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= aesbs_cbc_setkey,
	.encrypt		= cbc_encrypt,
	.decrypt		= cbc_decrypt,
}, {
	.base.cra_name		= "ctr(aes)",
	.base.cra_driver_name	= "ctr-aes-neonbs",
	.base.cra_priority	= 250,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct aesbs_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.chunksize		= AES_BLOCK_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= aesbs_setkey,
	.encrypt		= ctr_encrypt,
	.decrypt		= ctr_encrypt,
}, {
	.base.cra_name		= "xts(aes)",
	.base.cra_driver_name	= "xts-aes-neonbs",
	.base.cra_priority	= 250,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct aesbs_xts_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= 2 * AES_MIN_KEY_SIZE,
	.max_keysize		= 2 * AES_MAX_KEY_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= aesbs_xts_setkey,
	.encrypt		= xts_encrypt,
	.decrypt		= xts_decrypt,
} };

static void aes_exit(void)
{
	crypto_unregister_skciphers(aes_algs, ARRAY_SIZE(aes_algs));
}

static int __init aes_init(void)
{
	if (!(elf_hwcap & HWCAP_NEON))
		return -ENODEV;

	return crypto_register_skciphers(aes_algs, ARRAY_SIZE(aes_algs));
}

module_init(aes_init);
module_exit(aes_exit);
