// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bit sliced AES using NEON instructions
 *
 * Copyright (C) 2016 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/neon.h>
#include <asm/simd.h>
#include <crypto/aes.h>
#include <crypto/ctr.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <crypto/xts.h>
#include <linux/module.h>

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
				  int rounds, int blocks, u8 iv[]);

asmlinkage void aesbs_xts_encrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[]);
asmlinkage void aesbs_xts_decrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[]);

/* borrowed from aes-neon-blk.ko */
asmlinkage void neon_aes_ecb_encrypt(u8 out[], u8 const in[], u32 const rk[],
				     int rounds, int blocks);
asmlinkage void neon_aes_cbc_encrypt(u8 out[], u8 const in[], u32 const rk[],
				     int rounds, int blocks, u8 iv[]);
asmlinkage void neon_aes_ctr_encrypt(u8 out[], u8 const in[], u32 const rk[],
				     int rounds, int bytes, u8 ctr[]);
asmlinkage void neon_aes_xts_encrypt(u8 out[], u8 const in[],
				     u32 const rk1[], int rounds, int bytes,
				     u32 const rk2[], u8 iv[], int first);
asmlinkage void neon_aes_xts_decrypt(u8 out[], u8 const in[],
				     u32 const rk1[], int rounds, int bytes,
				     u32 const rk2[], u8 iv[], int first);

struct aesbs_ctx {
	u8	rk[13 * (8 * AES_BLOCK_SIZE) + 32];
	int	rounds;
} __aligned(AES_BLOCK_SIZE);

struct aesbs_cbc_ctr_ctx {
	struct aesbs_ctx	key;
	u32			enc[AES_MAX_KEYLENGTH_U32];
};

struct aesbs_xts_ctx {
	struct aesbs_ctx	key;
	u32			twkey[AES_MAX_KEYLENGTH_U32];
	struct crypto_aes_ctx	cts;
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

static int aesbs_cbc_ctr_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			    unsigned int key_len)
{
	struct aesbs_cbc_ctr_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_aes_ctx rk;
	int err;

	err = aes_expandkey(&rk, in_key, key_len);
	if (err)
		return err;

	ctx->key.rounds = 6 + key_len / 4;

	memcpy(ctx->enc, rk.key_enc, sizeof(ctx->enc));

	kernel_neon_begin();
	aesbs_convert_key(ctx->key.rk, rk.key_enc, ctx->key.rounds);
	kernel_neon_end();
	memzero_explicit(&rk, sizeof(rk));

	return 0;
}

static int cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_cbc_ctr_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes >= AES_BLOCK_SIZE) {
		unsigned int blocks = walk.nbytes / AES_BLOCK_SIZE;

		/* fall back to the non-bitsliced NEON implementation */
		kernel_neon_begin();
		neon_aes_cbc_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				     ctx->enc, ctx->key.rounds, blocks,
				     walk.iv);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	return err;
}

static int cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_cbc_ctr_ctx *ctx = crypto_skcipher_ctx(tfm);
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
	struct aesbs_cbc_ctr_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes > 0) {
		int blocks = (walk.nbytes / AES_BLOCK_SIZE) & ~7;
		int nbytes = walk.nbytes % (8 * AES_BLOCK_SIZE);
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;

		kernel_neon_begin();
		if (blocks >= 8) {
			aesbs_ctr_encrypt(dst, src, ctx->key.rk, ctx->key.rounds,
					  blocks, walk.iv);
			dst += blocks * AES_BLOCK_SIZE;
			src += blocks * AES_BLOCK_SIZE;
		}
		if (nbytes && walk.nbytes == walk.total) {
			u8 buf[AES_BLOCK_SIZE];
			u8 *d = dst;

			if (unlikely(nbytes < AES_BLOCK_SIZE))
				src = dst = memcpy(buf + sizeof(buf) - nbytes,
						   src, nbytes);

			neon_aes_ctr_encrypt(dst, src, ctx->enc, ctx->key.rounds,
					     nbytes, walk.iv);

			if (unlikely(nbytes < AES_BLOCK_SIZE))
				memcpy(d, dst, nbytes);

			nbytes = 0;
		}
		kernel_neon_end();
		err = skcipher_walk_done(&walk, nbytes);
	}
	return err;
}

static int aesbs_xts_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			    unsigned int key_len)
{
	struct aesbs_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_aes_ctx rk;
	int err;

	err = xts_verify_key(tfm, in_key, key_len);
	if (err)
		return err;

	key_len /= 2;
	err = aes_expandkey(&ctx->cts, in_key, key_len);
	if (err)
		return err;

	err = aes_expandkey(&rk, in_key + key_len, key_len);
	if (err)
		return err;

	memcpy(ctx->twkey, rk.key_enc, sizeof(ctx->twkey));

	return aesbs_setkey(tfm, in_key, key_len);
}

static int __xts_crypt(struct skcipher_request *req, bool encrypt,
		       void (*fn)(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[]))
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int tail = req->cryptlen % (8 * AES_BLOCK_SIZE);
	struct scatterlist sg_src[2], sg_dst[2];
	struct skcipher_request subreq;
	struct scatterlist *src, *dst;
	struct skcipher_walk walk;
	int nbytes, err;
	int first = 1;
	const u8 *in;
	u8 *out;

	if (req->cryptlen < AES_BLOCK_SIZE)
		return -EINVAL;

	/* ensure that the cts tail is covered by a single step */
	if (unlikely(tail > 0 && tail < AES_BLOCK_SIZE)) {
		int xts_blocks = DIV_ROUND_UP(req->cryptlen,
					      AES_BLOCK_SIZE) - 2;

		skcipher_request_set_tfm(&subreq, tfm);
		skcipher_request_set_callback(&subreq,
					      skcipher_request_flags(req),
					      NULL, NULL);
		skcipher_request_set_crypt(&subreq, req->src, req->dst,
					   xts_blocks * AES_BLOCK_SIZE,
					   req->iv);
		req = &subreq;
	} else {
		tail = 0;
	}

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		return err;

	while (walk.nbytes >= AES_BLOCK_SIZE) {
		int blocks = (walk.nbytes / AES_BLOCK_SIZE) & ~7;
		out = walk.dst.virt.addr;
		in = walk.src.virt.addr;
		nbytes = walk.nbytes;

		kernel_neon_begin();
		if (blocks >= 8) {
			if (first == 1)
				neon_aes_ecb_encrypt(walk.iv, walk.iv,
						     ctx->twkey,
						     ctx->key.rounds, 1);
			first = 2;

			fn(out, in, ctx->key.rk, ctx->key.rounds, blocks,
			   walk.iv);

			out += blocks * AES_BLOCK_SIZE;
			in += blocks * AES_BLOCK_SIZE;
			nbytes -= blocks * AES_BLOCK_SIZE;
		}
		if (walk.nbytes == walk.total && nbytes > 0) {
			if (encrypt)
				neon_aes_xts_encrypt(out, in, ctx->cts.key_enc,
						     ctx->key.rounds, nbytes,
						     ctx->twkey, walk.iv, first);
			else
				neon_aes_xts_decrypt(out, in, ctx->cts.key_dec,
						     ctx->key.rounds, nbytes,
						     ctx->twkey, walk.iv, first);
			nbytes = first = 0;
		}
		kernel_neon_end();
		err = skcipher_walk_done(&walk, nbytes);
	}

	if (err || likely(!tail))
		return err;

	/* handle ciphertext stealing */
	dst = src = scatterwalk_ffwd(sg_src, req->src, req->cryptlen);
	if (req->dst != req->src)
		dst = scatterwalk_ffwd(sg_dst, req->dst, req->cryptlen);

	skcipher_request_set_crypt(req, src, dst, AES_BLOCK_SIZE + tail,
				   req->iv);

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		return err;

	out = walk.dst.virt.addr;
	in = walk.src.virt.addr;
	nbytes = walk.nbytes;

	kernel_neon_begin();
	if (encrypt)
		neon_aes_xts_encrypt(out, in, ctx->cts.key_enc, ctx->key.rounds,
				     nbytes, ctx->twkey, walk.iv, first);
	else
		neon_aes_xts_decrypt(out, in, ctx->cts.key_dec, ctx->key.rounds,
				     nbytes, ctx->twkey, walk.iv, first);
	kernel_neon_end();

	return skcipher_walk_done(&walk, 0);
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
	.base.cra_ctxsize	= sizeof(struct aesbs_cbc_ctr_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= aesbs_cbc_ctr_setkey,
	.encrypt		= cbc_encrypt,
	.decrypt		= cbc_decrypt,
}, {
	.base.cra_name		= "ctr(aes)",
	.base.cra_driver_name	= "ctr-aes-neonbs",
	.base.cra_priority	= 250,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct aesbs_cbc_ctr_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.chunksize		= AES_BLOCK_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= aesbs_cbc_ctr_setkey,
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
	if (!cpu_have_named_feature(ASIMD))
		return -ENODEV;

	return crypto_register_skciphers(aes_algs, ARRAY_SIZE(aes_algs));
}

module_init(aes_init);
module_exit(aes_exit);
