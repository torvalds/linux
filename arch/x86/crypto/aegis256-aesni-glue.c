/*
 * The AEGIS-256 Authenticated-Encryption Algorithm
 *   Glue for AES-NI + SSE2 implementation
 *
 * Copyright (c) 2017-2018 Ondrej Mosnacek <omosnacek@gmail.com>
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
#include <crypto/scatterwalk.h>
#include <linux/module.h>
#include <asm/fpu/api.h>
#include <asm/cpu_device_id.h>

#define AEGIS256_BLOCK_ALIGN 16
#define AEGIS256_BLOCK_SIZE 16
#define AEGIS256_NONCE_SIZE 32
#define AEGIS256_STATE_BLOCKS 6
#define AEGIS256_KEY_SIZE 32
#define AEGIS256_MIN_AUTH_SIZE 8
#define AEGIS256_MAX_AUTH_SIZE 16

asmlinkage void crypto_aegis256_aesni_init(void *state, void *key, void *iv);

asmlinkage void crypto_aegis256_aesni_ad(
		void *state, unsigned int length, const void *data);

asmlinkage void crypto_aegis256_aesni_enc(
		void *state, unsigned int length, const void *src, void *dst);

asmlinkage void crypto_aegis256_aesni_dec(
		void *state, unsigned int length, const void *src, void *dst);

asmlinkage void crypto_aegis256_aesni_enc_tail(
		void *state, unsigned int length, const void *src, void *dst);

asmlinkage void crypto_aegis256_aesni_dec_tail(
		void *state, unsigned int length, const void *src, void *dst);

asmlinkage void crypto_aegis256_aesni_final(
		void *state, void *tag_xor, unsigned int cryptlen,
		unsigned int assoclen);

struct aegis_block {
	u8 bytes[AEGIS256_BLOCK_SIZE] __aligned(AEGIS256_BLOCK_ALIGN);
};

struct aegis_state {
	struct aegis_block blocks[AEGIS256_STATE_BLOCKS];
};

struct aegis_ctx {
	struct aegis_block key[AEGIS256_KEY_SIZE / AEGIS256_BLOCK_SIZE];
};

struct aegis_crypt_ops {
	int (*skcipher_walk_init)(struct skcipher_walk *walk,
				  struct aead_request *req, bool atomic);

	void (*crypt_blocks)(void *state, unsigned int length, const void *src,
			     void *dst);
	void (*crypt_tail)(void *state, unsigned int length, const void *src,
			   void *dst);
};

static void crypto_aegis256_aesni_process_ad(
		struct aegis_state *state, struct scatterlist *sg_src,
		unsigned int assoclen)
{
	struct scatter_walk walk;
	struct aegis_block buf;
	unsigned int pos = 0;

	scatterwalk_start(&walk, sg_src);
	while (assoclen != 0) {
		unsigned int size = scatterwalk_clamp(&walk, assoclen);
		unsigned int left = size;
		void *mapped = scatterwalk_map(&walk);
		const u8 *src = (const u8 *)mapped;

		if (pos + size >= AEGIS256_BLOCK_SIZE) {
			if (pos > 0) {
				unsigned int fill = AEGIS256_BLOCK_SIZE - pos;
				memcpy(buf.bytes + pos, src, fill);
				crypto_aegis256_aesni_ad(state,
							 AEGIS256_BLOCK_SIZE,
							 buf.bytes);
				pos = 0;
				left -= fill;
				src += fill;
			}

			crypto_aegis256_aesni_ad(state, left, src);

			src += left & ~(AEGIS256_BLOCK_SIZE - 1);
			left &= AEGIS256_BLOCK_SIZE - 1;
		}

		memcpy(buf.bytes + pos, src, left);
		pos += left;
		assoclen -= size;

		scatterwalk_unmap(mapped);
		scatterwalk_advance(&walk, size);
		scatterwalk_done(&walk, 0, assoclen);
	}

	if (pos > 0) {
		memset(buf.bytes + pos, 0, AEGIS256_BLOCK_SIZE - pos);
		crypto_aegis256_aesni_ad(state, AEGIS256_BLOCK_SIZE, buf.bytes);
	}
}

static void crypto_aegis256_aesni_process_crypt(
		struct aegis_state *state, struct skcipher_walk *walk,
		const struct aegis_crypt_ops *ops)
{
	while (walk->nbytes >= AEGIS256_BLOCK_SIZE) {
		ops->crypt_blocks(state,
				  round_down(walk->nbytes, AEGIS256_BLOCK_SIZE),
				  walk->src.virt.addr, walk->dst.virt.addr);
		skcipher_walk_done(walk, walk->nbytes % AEGIS256_BLOCK_SIZE);
	}

	if (walk->nbytes) {
		ops->crypt_tail(state, walk->nbytes, walk->src.virt.addr,
				walk->dst.virt.addr);
		skcipher_walk_done(walk, 0);
	}
}

static struct aegis_ctx *crypto_aegis256_aesni_ctx(struct crypto_aead *aead)
{
	u8 *ctx = crypto_aead_ctx(aead);
	ctx = PTR_ALIGN(ctx, __alignof__(struct aegis_ctx));
	return (void *)ctx;
}

static int crypto_aegis256_aesni_setkey(struct crypto_aead *aead, const u8 *key,
					unsigned int keylen)
{
	struct aegis_ctx *ctx = crypto_aegis256_aesni_ctx(aead);

	if (keylen != AEGIS256_KEY_SIZE) {
		crypto_aead_set_flags(aead, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memcpy(ctx->key, key, AEGIS256_KEY_SIZE);

	return 0;
}

static int crypto_aegis256_aesni_setauthsize(struct crypto_aead *tfm,
						unsigned int authsize)
{
	if (authsize > AEGIS256_MAX_AUTH_SIZE)
		return -EINVAL;
	if (authsize < AEGIS256_MIN_AUTH_SIZE)
		return -EINVAL;
	return 0;
}

static void crypto_aegis256_aesni_crypt(struct aead_request *req,
					struct aegis_block *tag_xor,
					unsigned int cryptlen,
					const struct aegis_crypt_ops *ops)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct aegis_ctx *ctx = crypto_aegis256_aesni_ctx(tfm);
	struct skcipher_walk walk;
	struct aegis_state state;

	ops->skcipher_walk_init(&walk, req, true);

	kernel_fpu_begin();

	crypto_aegis256_aesni_init(&state, ctx->key, req->iv);
	crypto_aegis256_aesni_process_ad(&state, req->src, req->assoclen);
	crypto_aegis256_aesni_process_crypt(&state, &walk, ops);
	crypto_aegis256_aesni_final(&state, tag_xor, req->assoclen, cryptlen);

	kernel_fpu_end();
}

static int crypto_aegis256_aesni_encrypt(struct aead_request *req)
{
	static const struct aegis_crypt_ops OPS = {
		.skcipher_walk_init = skcipher_walk_aead_encrypt,
		.crypt_blocks = crypto_aegis256_aesni_enc,
		.crypt_tail = crypto_aegis256_aesni_enc_tail,
	};

	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct aegis_block tag = {};
	unsigned int authsize = crypto_aead_authsize(tfm);
	unsigned int cryptlen = req->cryptlen;

	crypto_aegis256_aesni_crypt(req, &tag, cryptlen, &OPS);

	scatterwalk_map_and_copy(tag.bytes, req->dst,
				 req->assoclen + cryptlen, authsize, 1);
	return 0;
}

static int crypto_aegis256_aesni_decrypt(struct aead_request *req)
{
	static const struct aegis_block zeros = {};

	static const struct aegis_crypt_ops OPS = {
		.skcipher_walk_init = skcipher_walk_aead_decrypt,
		.crypt_blocks = crypto_aegis256_aesni_dec,
		.crypt_tail = crypto_aegis256_aesni_dec_tail,
	};

	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct aegis_block tag;
	unsigned int authsize = crypto_aead_authsize(tfm);
	unsigned int cryptlen = req->cryptlen - authsize;

	scatterwalk_map_and_copy(tag.bytes, req->src,
				 req->assoclen + cryptlen, authsize, 0);

	crypto_aegis256_aesni_crypt(req, &tag, cryptlen, &OPS);

	return crypto_memneq(tag.bytes, zeros.bytes, authsize) ? -EBADMSG : 0;
}

static int crypto_aegis256_aesni_init_tfm(struct crypto_aead *aead)
{
	return 0;
}

static void crypto_aegis256_aesni_exit_tfm(struct crypto_aead *aead)
{
}

static int cryptd_aegis256_aesni_setkey(struct crypto_aead *aead,
					const u8 *key, unsigned int keylen)
{
	struct cryptd_aead **ctx = crypto_aead_ctx(aead);
	struct cryptd_aead *cryptd_tfm = *ctx;

	return crypto_aead_setkey(&cryptd_tfm->base, key, keylen);
}

static int cryptd_aegis256_aesni_setauthsize(struct crypto_aead *aead,
					     unsigned int authsize)
{
	struct cryptd_aead **ctx = crypto_aead_ctx(aead);
	struct cryptd_aead *cryptd_tfm = *ctx;

	return crypto_aead_setauthsize(&cryptd_tfm->base, authsize);
}

static int cryptd_aegis256_aesni_encrypt(struct aead_request *req)
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

static int cryptd_aegis256_aesni_decrypt(struct aead_request *req)
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

static int cryptd_aegis256_aesni_init_tfm(struct crypto_aead *aead)
{
	struct cryptd_aead *cryptd_tfm;
	struct cryptd_aead **ctx = crypto_aead_ctx(aead);

	cryptd_tfm = cryptd_alloc_aead("__aegis256-aesni", CRYPTO_ALG_INTERNAL,
				       CRYPTO_ALG_INTERNAL);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);

	*ctx = cryptd_tfm;
	crypto_aead_set_reqsize(aead, crypto_aead_reqsize(&cryptd_tfm->base));
	return 0;
}

static void cryptd_aegis256_aesni_exit_tfm(struct crypto_aead *aead)
{
	struct cryptd_aead **ctx = crypto_aead_ctx(aead);

	cryptd_free_aead(*ctx);
}

static struct aead_alg crypto_aegis256_aesni_alg[] = {
	{
		.setkey = crypto_aegis256_aesni_setkey,
		.setauthsize = crypto_aegis256_aesni_setauthsize,
		.encrypt = crypto_aegis256_aesni_encrypt,
		.decrypt = crypto_aegis256_aesni_decrypt,
		.init = crypto_aegis256_aesni_init_tfm,
		.exit = crypto_aegis256_aesni_exit_tfm,

		.ivsize = AEGIS256_NONCE_SIZE,
		.maxauthsize = AEGIS256_MAX_AUTH_SIZE,
		.chunksize = AEGIS256_BLOCK_SIZE,

		.base = {
			.cra_flags = CRYPTO_ALG_INTERNAL,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct aegis_ctx) +
				__alignof__(struct aegis_ctx),
			.cra_alignmask = 0,

			.cra_name = "__aegis256",
			.cra_driver_name = "__aegis256-aesni",

			.cra_module = THIS_MODULE,
		}
	}, {
		.setkey = cryptd_aegis256_aesni_setkey,
		.setauthsize = cryptd_aegis256_aesni_setauthsize,
		.encrypt = cryptd_aegis256_aesni_encrypt,
		.decrypt = cryptd_aegis256_aesni_decrypt,
		.init = cryptd_aegis256_aesni_init_tfm,
		.exit = cryptd_aegis256_aesni_exit_tfm,

		.ivsize = AEGIS256_NONCE_SIZE,
		.maxauthsize = AEGIS256_MAX_AUTH_SIZE,
		.chunksize = AEGIS256_BLOCK_SIZE,

		.base = {
			.cra_flags = CRYPTO_ALG_ASYNC,
			.cra_blocksize = 1,
			.cra_ctxsize = sizeof(struct cryptd_aead *),
			.cra_alignmask = 0,

			.cra_priority = 400,

			.cra_name = "aegis256",
			.cra_driver_name = "aegis256-aesni",

			.cra_module = THIS_MODULE,
		}
	}
};

static int __init crypto_aegis256_aesni_module_init(void)
{
	if (!boot_cpu_has(X86_FEATURE_XMM2) ||
	    !boot_cpu_has(X86_FEATURE_AES) ||
	    !cpu_has_xfeatures(XFEATURE_MASK_SSE, NULL))
		return -ENODEV;

	return crypto_register_aeads(crypto_aegis256_aesni_alg,
				    ARRAY_SIZE(crypto_aegis256_aesni_alg));
}

static void __exit crypto_aegis256_aesni_module_exit(void)
{
	crypto_unregister_aeads(crypto_aegis256_aesni_alg,
				ARRAY_SIZE(crypto_aegis256_aesni_alg));
}

module_init(crypto_aegis256_aesni_module_init);
module_exit(crypto_aegis256_aesni_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ondrej Mosnacek <omosnacek@gmail.com>");
MODULE_DESCRIPTION("AEGIS-256 AEAD algorithm -- AESNI+SSE2 implementation");
MODULE_ALIAS_CRYPTO("aegis256");
MODULE_ALIAS_CRYPTO("aegis256-aesni");
