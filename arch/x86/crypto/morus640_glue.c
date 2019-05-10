/*
 * The MORUS-640 Authenticated-Encryption Algorithm
 *   Common x86 SIMD glue skeleton
 *
 * Copyright (c) 2016-2018 Ondrej Mosnacek <omosnacek@gmail.com>
 * Copyright (C) 2017-2018 Red Hat, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <crypto/cryptd.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/morus640_glue.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <asm/fpu/api.h>

struct morus640_state {
	struct morus640_block s[MORUS_STATE_BLOCKS];
};

struct morus640_ops {
	int (*skcipher_walk_init)(struct skcipher_walk *walk,
				  struct aead_request *req, bool atomic);

	void (*crypt_blocks)(void *state, const void *src, void *dst,
			     unsigned int length);
	void (*crypt_tail)(void *state, const void *src, void *dst,
			   unsigned int length);
};

static void crypto_morus640_glue_process_ad(
		struct morus640_state *state,
		const struct morus640_glue_ops *ops,
		struct scatterlist *sg_src, unsigned int assoclen)
{
	struct scatter_walk walk;
	struct morus640_block buf;
	unsigned int pos = 0;

	scatterwalk_start(&walk, sg_src);
	while (assoclen != 0) {
		unsigned int size = scatterwalk_clamp(&walk, assoclen);
		unsigned int left = size;
		void *mapped = scatterwalk_map(&walk);
		const u8 *src = (const u8 *)mapped;

		if (pos + size >= MORUS640_BLOCK_SIZE) {
			if (pos > 0) {
				unsigned int fill = MORUS640_BLOCK_SIZE - pos;
				memcpy(buf.bytes + pos, src, fill);
				ops->ad(state, buf.bytes, MORUS640_BLOCK_SIZE);
				pos = 0;
				left -= fill;
				src += fill;
			}

			ops->ad(state, src, left);
			src += left & ~(MORUS640_BLOCK_SIZE - 1);
			left &= MORUS640_BLOCK_SIZE - 1;
		}

		memcpy(buf.bytes + pos, src, left);

		pos += left;
		assoclen -= size;
		scatterwalk_unmap(mapped);
		scatterwalk_advance(&walk, size);
		scatterwalk_done(&walk, 0, assoclen);
	}

	if (pos > 0) {
		memset(buf.bytes + pos, 0, MORUS640_BLOCK_SIZE - pos);
		ops->ad(state, buf.bytes, MORUS640_BLOCK_SIZE);
	}
}

static void crypto_morus640_glue_process_crypt(struct morus640_state *state,
					       struct morus640_ops ops,
					       struct skcipher_walk *walk)
{
	while (walk->nbytes >= MORUS640_BLOCK_SIZE) {
		ops.crypt_blocks(state, walk->src.virt.addr,
				 walk->dst.virt.addr,
				 round_down(walk->nbytes, MORUS640_BLOCK_SIZE));
		skcipher_walk_done(walk, walk->nbytes % MORUS640_BLOCK_SIZE);
	}

	if (walk->nbytes) {
		ops.crypt_tail(state, walk->src.virt.addr, walk->dst.virt.addr,
			       walk->nbytes);
		skcipher_walk_done(walk, 0);
	}
}

int crypto_morus640_glue_setkey(struct crypto_aead *aead, const u8 *key,
				unsigned int keylen)
{
	struct morus640_ctx *ctx = crypto_aead_ctx(aead);

	if (keylen != MORUS640_BLOCK_SIZE) {
		crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memcpy(ctx->key.bytes, key, MORUS640_BLOCK_SIZE);
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_morus640_glue_setkey);

int crypto_morus640_glue_setauthsize(struct crypto_aead *tfm,
				     unsigned int authsize)
{
	return (authsize <= MORUS_MAX_AUTH_SIZE) ? 0 : -EINVAL;
}
EXPORT_SYMBOL_GPL(crypto_morus640_glue_setauthsize);

static void crypto_morus640_glue_crypt(struct aead_request *req,
				       struct morus640_ops ops,
				       unsigned int cryptlen,
				       struct morus640_block *tag_xor)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct morus640_ctx *ctx = crypto_aead_ctx(tfm);
	struct morus640_state state;
	struct skcipher_walk walk;

	ops.skcipher_walk_init(&walk, req, true);

	kernel_fpu_begin();

	ctx->ops->init(&state, &ctx->key, req->iv);
	crypto_morus640_glue_process_ad(&state, ctx->ops, req->src, req->assoclen);
	crypto_morus640_glue_process_crypt(&state, ops, &walk);
	ctx->ops->final(&state, tag_xor, req->assoclen, cryptlen);

	kernel_fpu_end();
}

int crypto_morus640_glue_encrypt(struct aead_request *req)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct morus640_ctx *ctx = crypto_aead_ctx(tfm);
	struct morus640_ops OPS = {
		.skcipher_walk_init = skcipher_walk_aead_encrypt,
		.crypt_blocks = ctx->ops->enc,
		.crypt_tail = ctx->ops->enc_tail,
	};

	struct morus640_block tag = {};
	unsigned int authsize = crypto_aead_authsize(tfm);
	unsigned int cryptlen = req->cryptlen;

	crypto_morus640_glue_crypt(req, OPS, cryptlen, &tag);

	scatterwalk_map_and_copy(tag.bytes, req->dst,
				 req->assoclen + cryptlen, authsize, 1);
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_morus640_glue_encrypt);

int crypto_morus640_glue_decrypt(struct aead_request *req)
{
	static const u8 zeros[MORUS640_BLOCK_SIZE] = {};

	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct morus640_ctx *ctx = crypto_aead_ctx(tfm);
	struct morus640_ops OPS = {
		.skcipher_walk_init = skcipher_walk_aead_decrypt,
		.crypt_blocks = ctx->ops->dec,
		.crypt_tail = ctx->ops->dec_tail,
	};

	struct morus640_block tag;
	unsigned int authsize = crypto_aead_authsize(tfm);
	unsigned int cryptlen = req->cryptlen - authsize;

	scatterwalk_map_and_copy(tag.bytes, req->src,
				 req->assoclen + cryptlen, authsize, 0);

	crypto_morus640_glue_crypt(req, OPS, cryptlen, &tag);

	return crypto_memneq(tag.bytes, zeros, authsize) ? -EBADMSG : 0;
}
EXPORT_SYMBOL_GPL(crypto_morus640_glue_decrypt);

void crypto_morus640_glue_init_ops(struct crypto_aead *aead,
				   const struct morus640_glue_ops *ops)
{
	struct morus640_ctx *ctx = crypto_aead_ctx(aead);
	ctx->ops = ops;
}
EXPORT_SYMBOL_GPL(crypto_morus640_glue_init_ops);

int cryptd_morus640_glue_setkey(struct crypto_aead *aead, const u8 *key,
				unsigned int keylen)
{
	struct cryptd_aead **ctx = crypto_aead_ctx(aead);
	struct cryptd_aead *cryptd_tfm = *ctx;

	return crypto_aead_setkey(&cryptd_tfm->base, key, keylen);
}
EXPORT_SYMBOL_GPL(cryptd_morus640_glue_setkey);

int cryptd_morus640_glue_setauthsize(struct crypto_aead *aead,
				     unsigned int authsize)
{
	struct cryptd_aead **ctx = crypto_aead_ctx(aead);
	struct cryptd_aead *cryptd_tfm = *ctx;

	return crypto_aead_setauthsize(&cryptd_tfm->base, authsize);
}
EXPORT_SYMBOL_GPL(cryptd_morus640_glue_setauthsize);

int cryptd_morus640_glue_encrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct cryptd_aead **ctx = crypto_aead_ctx(aead);
	struct cryptd_aead *cryptd_tfm = *ctx;

	aead = &cryptd_tfm->base;
	if (irq_fpu_usable() && (!in_atomic() ||
				 !cryptd_aead_queued(cryptd_tfm)))
		aead = cryptd_aead_child(cryptd_tfm);

	aead_request_set_tfm(req, aead);

	return crypto_aead_encrypt(req);
}
EXPORT_SYMBOL_GPL(cryptd_morus640_glue_encrypt);

int cryptd_morus640_glue_decrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct cryptd_aead **ctx = crypto_aead_ctx(aead);
	struct cryptd_aead *cryptd_tfm = *ctx;

	aead = &cryptd_tfm->base;
	if (irq_fpu_usable() && (!in_atomic() ||
				 !cryptd_aead_queued(cryptd_tfm)))
		aead = cryptd_aead_child(cryptd_tfm);

	aead_request_set_tfm(req, aead);

	return crypto_aead_decrypt(req);
}
EXPORT_SYMBOL_GPL(cryptd_morus640_glue_decrypt);

int cryptd_morus640_glue_init_tfm(struct crypto_aead *aead)
{
	struct cryptd_aead *cryptd_tfm;
	struct cryptd_aead **ctx = crypto_aead_ctx(aead);
	const char *name = crypto_aead_alg(aead)->base.cra_driver_name;
	char internal_name[CRYPTO_MAX_ALG_NAME];

	if (snprintf(internal_name, CRYPTO_MAX_ALG_NAME, "__%s", name)
			>= CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	cryptd_tfm = cryptd_alloc_aead(internal_name, CRYPTO_ALG_INTERNAL,
				       CRYPTO_ALG_INTERNAL);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);

	*ctx = cryptd_tfm;
	crypto_aead_set_reqsize(aead, crypto_aead_reqsize(&cryptd_tfm->base));
	return 0;
}
EXPORT_SYMBOL_GPL(cryptd_morus640_glue_init_tfm);

void cryptd_morus640_glue_exit_tfm(struct crypto_aead *aead)
{
	struct cryptd_aead **ctx = crypto_aead_ctx(aead);

	cryptd_free_aead(*ctx);
}
EXPORT_SYMBOL_GPL(cryptd_morus640_glue_exit_tfm);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ondrej Mosnacek <omosnacek@gmail.com>");
MODULE_DESCRIPTION("MORUS-640 AEAD mode -- glue for x86 optimizations");
