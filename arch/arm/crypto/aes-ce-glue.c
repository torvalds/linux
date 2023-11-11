// SPDX-License-Identifier: GPL-2.0-only
/*
 * aes-ce-glue.c - wrapper code for ARMv8 AES
 *
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>
#include <asm/unaligned.h>
#include <crypto/aes.h>
#include <crypto/ctr.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/cpufeature.h>
#include <linux/module.h>
#include <crypto/xts.h>

MODULE_DESCRIPTION("AES-ECB/CBC/CTR/XTS using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

/* defined in aes-ce-core.S */
asmlinkage u32 ce_aes_sub(u32 input);
asmlinkage void ce_aes_invert(void *dst, void *src);

asmlinkage void ce_aes_ecb_encrypt(u8 out[], u8 const in[], u32 const rk[],
				   int rounds, int blocks);
asmlinkage void ce_aes_ecb_decrypt(u8 out[], u8 const in[], u32 const rk[],
				   int rounds, int blocks);

asmlinkage void ce_aes_cbc_encrypt(u8 out[], u8 const in[], u32 const rk[],
				   int rounds, int blocks, u8 iv[]);
asmlinkage void ce_aes_cbc_decrypt(u8 out[], u8 const in[], u32 const rk[],
				   int rounds, int blocks, u8 iv[]);
asmlinkage void ce_aes_cbc_cts_encrypt(u8 out[], u8 const in[], u32 const rk[],
				   int rounds, int bytes, u8 const iv[]);
asmlinkage void ce_aes_cbc_cts_decrypt(u8 out[], u8 const in[], u32 const rk[],
				   int rounds, int bytes, u8 const iv[]);

asmlinkage void ce_aes_ctr_encrypt(u8 out[], u8 const in[], u32 const rk[],
				   int rounds, int blocks, u8 ctr[]);

asmlinkage void ce_aes_xts_encrypt(u8 out[], u8 const in[], u32 const rk1[],
				   int rounds, int bytes, u8 iv[],
				   u32 const rk2[], int first);
asmlinkage void ce_aes_xts_decrypt(u8 out[], u8 const in[], u32 const rk1[],
				   int rounds, int bytes, u8 iv[],
				   u32 const rk2[], int first);

struct aes_block {
	u8 b[AES_BLOCK_SIZE];
};

static int num_rounds(struct crypto_aes_ctx *ctx)
{
	/*
	 * # of rounds specified by AES:
	 * 128 bit key		10 rounds
	 * 192 bit key		12 rounds
	 * 256 bit key		14 rounds
	 * => n byte key	=> 6 + (n/4) rounds
	 */
	return 6 + ctx->key_length / 4;
}

static int ce_aes_expandkey(struct crypto_aes_ctx *ctx, const u8 *in_key,
			    unsigned int key_len)
{
	/*
	 * The AES key schedule round constants
	 */
	static u8 const rcon[] = {
		0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36,
	};

	u32 kwords = key_len / sizeof(u32);
	struct aes_block *key_enc, *key_dec;
	int i, j;

	if (key_len != AES_KEYSIZE_128 &&
	    key_len != AES_KEYSIZE_192 &&
	    key_len != AES_KEYSIZE_256)
		return -EINVAL;

	ctx->key_length = key_len;
	for (i = 0; i < kwords; i++)
		ctx->key_enc[i] = get_unaligned_le32(in_key + i * sizeof(u32));

	kernel_neon_begin();
	for (i = 0; i < sizeof(rcon); i++) {
		u32 *rki = ctx->key_enc + (i * kwords);
		u32 *rko = rki + kwords;

		rko[0] = ror32(ce_aes_sub(rki[kwords - 1]), 8);
		rko[0] = rko[0] ^ rki[0] ^ rcon[i];
		rko[1] = rko[0] ^ rki[1];
		rko[2] = rko[1] ^ rki[2];
		rko[3] = rko[2] ^ rki[3];

		if (key_len == AES_KEYSIZE_192) {
			if (i >= 7)
				break;
			rko[4] = rko[3] ^ rki[4];
			rko[5] = rko[4] ^ rki[5];
		} else if (key_len == AES_KEYSIZE_256) {
			if (i >= 6)
				break;
			rko[4] = ce_aes_sub(rko[3]) ^ rki[4];
			rko[5] = rko[4] ^ rki[5];
			rko[6] = rko[5] ^ rki[6];
			rko[7] = rko[6] ^ rki[7];
		}
	}

	/*
	 * Generate the decryption keys for the Equivalent Inverse Cipher.
	 * This involves reversing the order of the round keys, and applying
	 * the Inverse Mix Columns transformation on all but the first and
	 * the last one.
	 */
	key_enc = (struct aes_block *)ctx->key_enc;
	key_dec = (struct aes_block *)ctx->key_dec;
	j = num_rounds(ctx);

	key_dec[0] = key_enc[j];
	for (i = 1, j--; j > 0; i++, j--)
		ce_aes_invert(key_dec + i, key_enc + j);
	key_dec[i] = key_enc[0];

	kernel_neon_end();
	return 0;
}

static int ce_aes_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			 unsigned int key_len)
{
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);

	return ce_aes_expandkey(ctx, in_key, key_len);
}

struct crypto_aes_xts_ctx {
	struct crypto_aes_ctx key1;
	struct crypto_aes_ctx __aligned(8) key2;
};

static int xts_set_key(struct crypto_skcipher *tfm, const u8 *in_key,
		       unsigned int key_len)
{
	struct crypto_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;

	ret = xts_verify_key(tfm, in_key, key_len);
	if (ret)
		return ret;

	ret = ce_aes_expandkey(&ctx->key1, in_key, key_len / 2);
	if (!ret)
		ret = ce_aes_expandkey(&ctx->key2, &in_key[key_len / 2],
				       key_len / 2);
	return ret;
}

static int ecb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int blocks;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((blocks = (walk.nbytes / AES_BLOCK_SIZE))) {
		kernel_neon_begin();
		ce_aes_ecb_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				   ctx->key_enc, num_rounds(ctx), blocks);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	return err;
}

static int ecb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int blocks;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((blocks = (walk.nbytes / AES_BLOCK_SIZE))) {
		kernel_neon_begin();
		ce_aes_ecb_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
				   ctx->key_dec, num_rounds(ctx), blocks);
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
	unsigned int blocks;
	int err = 0;

	while ((blocks = (walk->nbytes / AES_BLOCK_SIZE))) {
		kernel_neon_begin();
		ce_aes_cbc_encrypt(walk->dst.virt.addr, walk->src.virt.addr,
				   ctx->key_enc, num_rounds(ctx), blocks,
				   walk->iv);
		kernel_neon_end();
		err = skcipher_walk_done(walk, walk->nbytes % AES_BLOCK_SIZE);
	}
	return err;
}

static int cbc_encrypt(struct skcipher_request *req)
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
	unsigned int blocks;
	int err = 0;

	while ((blocks = (walk->nbytes / AES_BLOCK_SIZE))) {
		kernel_neon_begin();
		ce_aes_cbc_decrypt(walk->dst.virt.addr, walk->src.virt.addr,
				   ctx->key_dec, num_rounds(ctx), blocks,
				   walk->iv);
		kernel_neon_end();
		err = skcipher_walk_done(walk, walk->nbytes % AES_BLOCK_SIZE);
	}
	return err;
}

static int cbc_decrypt(struct skcipher_request *req)
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
	int cbc_blocks = DIV_ROUND_UP(req->cryptlen, AES_BLOCK_SIZE) - 2;
	struct scatterlist *src = req->src, *dst = req->dst;
	struct scatterlist sg_src[2], sg_dst[2];
	struct skcipher_request subreq;
	struct skcipher_walk walk;
	int err;

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
	ce_aes_cbc_cts_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
			       ctx->key_enc, num_rounds(ctx), walk.nbytes,
			       walk.iv);
	kernel_neon_end();

	return skcipher_walk_done(&walk, 0);
}

static int cts_cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int cbc_blocks = DIV_ROUND_UP(req->cryptlen, AES_BLOCK_SIZE) - 2;
	struct scatterlist *src = req->src, *dst = req->dst;
	struct scatterlist sg_src[2], sg_dst[2];
	struct skcipher_request subreq;
	struct skcipher_walk walk;
	int err;

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
	ce_aes_cbc_cts_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
			       ctx->key_dec, num_rounds(ctx), walk.nbytes,
			       walk.iv);
	kernel_neon_end();

	return skcipher_walk_done(&walk, 0);
}

static int ctr_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	int err, blocks;

	err = skcipher_walk_virt(&walk, req, false);

	while ((blocks = (walk.nbytes / AES_BLOCK_SIZE))) {
		kernel_neon_begin();
		ce_aes_ctr_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				   ctx->key_enc, num_rounds(ctx), blocks,
				   walk.iv);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	if (walk.nbytes) {
		u8 __aligned(8) tail[AES_BLOCK_SIZE];
		unsigned int nbytes = walk.nbytes;
		u8 *tdst = walk.dst.virt.addr;
		u8 *tsrc = walk.src.virt.addr;

		/*
		 * Tell aes_ctr_encrypt() to process a tail block.
		 */
		blocks = -1;

		kernel_neon_begin();
		ce_aes_ctr_encrypt(tail, NULL, ctx->key_enc, num_rounds(ctx),
				   blocks, walk.iv);
		kernel_neon_end();
		crypto_xor_cpy(tdst, tsrc, tail, nbytes);
		err = skcipher_walk_done(&walk, 0);
	}
	return err;
}

static void ctr_encrypt_one(struct crypto_skcipher *tfm, const u8 *src, u8 *dst)
{
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
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

static int ctr_encrypt_sync(struct skcipher_request *req)
{
	if (!crypto_simd_usable())
		return crypto_ctr_encrypt_walk(req, ctr_encrypt_one);

	return ctr_encrypt(req);
}

static int xts_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, first, rounds = num_rounds(&ctx->key1);
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
		ce_aes_xts_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				   ctx->key1.key_enc, rounds, nbytes, walk.iv,
				   ctx->key2.key_enc, first);
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

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		return err;

	kernel_neon_begin();
	ce_aes_xts_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
			   ctx->key1.key_enc, rounds, walk.nbytes, walk.iv,
			   ctx->key2.key_enc, first);
	kernel_neon_end();

	return skcipher_walk_done(&walk, 0);
}

static int xts_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, first, rounds = num_rounds(&ctx->key1);
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
		ce_aes_xts_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
				   ctx->key1.key_dec, rounds, nbytes, walk.iv,
				   ctx->key2.key_enc, first);
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

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		return err;

	kernel_neon_begin();
	ce_aes_xts_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
			   ctx->key1.key_dec, rounds, walk.nbytes, walk.iv,
			   ctx->key2.key_enc, first);
	kernel_neon_end();

	return skcipher_walk_done(&walk, 0);
}

static struct skcipher_alg aes_algs[] = { {
	.base.cra_name		= "__ecb(aes)",
	.base.cra_driver_name	= "__ecb-aes-ce",
	.base.cra_priority	= 300,
	.base.cra_flags		= CRYPTO_ALG_INTERNAL,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct crypto_aes_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.setkey			= ce_aes_setkey,
	.encrypt		= ecb_encrypt,
	.decrypt		= ecb_decrypt,
}, {
	.base.cra_name		= "__cbc(aes)",
	.base.cra_driver_name	= "__cbc-aes-ce",
	.base.cra_priority	= 300,
	.base.cra_flags		= CRYPTO_ALG_INTERNAL,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct crypto_aes_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= ce_aes_setkey,
	.encrypt		= cbc_encrypt,
	.decrypt		= cbc_decrypt,
}, {
	.base.cra_name		= "__cts(cbc(aes))",
	.base.cra_driver_name	= "__cts-cbc-aes-ce",
	.base.cra_priority	= 300,
	.base.cra_flags		= CRYPTO_ALG_INTERNAL,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct crypto_aes_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.walksize		= 2 * AES_BLOCK_SIZE,
	.setkey			= ce_aes_setkey,
	.encrypt		= cts_cbc_encrypt,
	.decrypt		= cts_cbc_decrypt,
}, {
	.base.cra_name		= "__ctr(aes)",
	.base.cra_driver_name	= "__ctr-aes-ce",
	.base.cra_priority	= 300,
	.base.cra_flags		= CRYPTO_ALG_INTERNAL,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct crypto_aes_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.chunksize		= AES_BLOCK_SIZE,
	.setkey			= ce_aes_setkey,
	.encrypt		= ctr_encrypt,
	.decrypt		= ctr_encrypt,
}, {
	.base.cra_name		= "ctr(aes)",
	.base.cra_driver_name	= "ctr-aes-ce-sync",
	.base.cra_priority	= 300 - 1,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct crypto_aes_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.chunksize		= AES_BLOCK_SIZE,
	.setkey			= ce_aes_setkey,
	.encrypt		= ctr_encrypt_sync,
	.decrypt		= ctr_encrypt_sync,
}, {
	.base.cra_name		= "__xts(aes)",
	.base.cra_driver_name	= "__xts-aes-ce",
	.base.cra_priority	= 300,
	.base.cra_flags		= CRYPTO_ALG_INTERNAL,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct crypto_aes_xts_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= 2 * AES_MIN_KEY_SIZE,
	.max_keysize		= 2 * AES_MAX_KEY_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.walksize		= 2 * AES_BLOCK_SIZE,
	.setkey			= xts_set_key,
	.encrypt		= xts_encrypt,
	.decrypt		= xts_decrypt,
} };

static struct simd_skcipher_alg *aes_simd_algs[ARRAY_SIZE(aes_algs)];

static void aes_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(aes_simd_algs) && aes_simd_algs[i]; i++)
		simd_skcipher_free(aes_simd_algs[i]);

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
}

module_cpu_feature_match(AES, aes_init);
module_exit(aes_exit);
