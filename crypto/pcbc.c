/*
 * PCBC: Propagating Cipher Block Chaining mode
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * Derived from cbc.c
 * - Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

struct crypto_pcbc_ctx {
	struct crypto_cipher *child;
};

static int crypto_pcbc_setkey(struct crypto_skcipher *parent, const u8 *key,
			      unsigned int keylen)
{
	struct crypto_pcbc_ctx *ctx = crypto_skcipher_ctx(parent);
	struct crypto_cipher *child = ctx->child;
	int err;

	crypto_cipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_cipher_set_flags(child, crypto_skcipher_get_flags(parent) &
				       CRYPTO_TFM_REQ_MASK);
	err = crypto_cipher_setkey(child, key, keylen);
	crypto_skcipher_set_flags(parent, crypto_cipher_get_flags(child) &
					  CRYPTO_TFM_RES_MASK);
	return err;
}

static int crypto_pcbc_encrypt_segment(struct skcipher_request *req,
				       struct skcipher_walk *walk,
				       struct crypto_cipher *tfm)
{
	int bsize = crypto_cipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	u8 *iv = walk->iv;

	do {
		crypto_xor(iv, src, bsize);
		crypto_cipher_encrypt_one(tfm, dst, iv);
		memcpy(iv, dst, bsize);
		crypto_xor(iv, src, bsize);

		src += bsize;
		dst += bsize;
	} while ((nbytes -= bsize) >= bsize);

	return nbytes;
}

static int crypto_pcbc_encrypt_inplace(struct skcipher_request *req,
				       struct skcipher_walk *walk,
				       struct crypto_cipher *tfm)
{
	int bsize = crypto_cipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *iv = walk->iv;
	u8 tmpbuf[bsize];

	do {
		memcpy(tmpbuf, src, bsize);
		crypto_xor(iv, src, bsize);
		crypto_cipher_encrypt_one(tfm, src, iv);
		memcpy(iv, tmpbuf, bsize);
		crypto_xor(iv, src, bsize);

		src += bsize;
	} while ((nbytes -= bsize) >= bsize);

	memcpy(walk->iv, iv, bsize);

	return nbytes;
}

static int crypto_pcbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_pcbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_cipher *child = ctx->child;
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes)) {
		if (walk.src.virt.addr == walk.dst.virt.addr)
			nbytes = crypto_pcbc_encrypt_inplace(req, &walk,
							     child);
		else
			nbytes = crypto_pcbc_encrypt_segment(req, &walk,
							     child);
		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int crypto_pcbc_decrypt_segment(struct skcipher_request *req,
				       struct skcipher_walk *walk,
				       struct crypto_cipher *tfm)
{
	int bsize = crypto_cipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	u8 *iv = walk->iv;

	do {
		crypto_cipher_decrypt_one(tfm, dst, src);
		crypto_xor(dst, iv, bsize);
		memcpy(iv, src, bsize);
		crypto_xor(iv, dst, bsize);

		src += bsize;
		dst += bsize;
	} while ((nbytes -= bsize) >= bsize);

	memcpy(walk->iv, iv, bsize);

	return nbytes;
}

static int crypto_pcbc_decrypt_inplace(struct skcipher_request *req,
				       struct skcipher_walk *walk,
				       struct crypto_cipher *tfm)
{
	int bsize = crypto_cipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *iv = walk->iv;
	u8 tmpbuf[bsize] __attribute__ ((aligned(__alignof__(u32))));

	do {
		memcpy(tmpbuf, src, bsize);
		crypto_cipher_decrypt_one(tfm, src, src);
		crypto_xor(src, iv, bsize);
		memcpy(iv, tmpbuf, bsize);
		crypto_xor(iv, src, bsize);

		src += bsize;
	} while ((nbytes -= bsize) >= bsize);

	memcpy(walk->iv, iv, bsize);

	return nbytes;
}

static int crypto_pcbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_pcbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_cipher *child = ctx->child;
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes)) {
		if (walk.src.virt.addr == walk.dst.virt.addr)
			nbytes = crypto_pcbc_decrypt_inplace(req, &walk,
							     child);
		else
			nbytes = crypto_pcbc_decrypt_segment(req, &walk,
							     child);
		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int crypto_pcbc_init_tfm(struct crypto_skcipher *tfm)
{
	struct skcipher_instance *inst = skcipher_alg_instance(tfm);
	struct crypto_spawn *spawn = skcipher_instance_ctx(inst);
	struct crypto_pcbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_cipher *cipher;

	cipher = crypto_spawn_cipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;
	return 0;
}

static void crypto_pcbc_exit_tfm(struct crypto_skcipher *tfm)
{
	struct crypto_pcbc_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_cipher(ctx->child);
}

static void crypto_pcbc_free(struct skcipher_instance *inst)
{
	crypto_drop_skcipher(skcipher_instance_ctx(inst));
	kfree(inst);
}

static int crypto_pcbc_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct skcipher_instance *inst;
	struct crypto_attr_type *algt;
	struct crypto_spawn *spawn;
	struct crypto_alg *alg;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	if (((algt->type ^ CRYPTO_ALG_TYPE_SKCIPHER) & algt->mask) &
	    ~CRYPTO_ALG_INTERNAL)
		return -EINVAL;

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	alg = crypto_get_attr_alg(tb, CRYPTO_ALG_TYPE_CIPHER |
				      (algt->type & CRYPTO_ALG_INTERNAL),
				  CRYPTO_ALG_TYPE_MASK |
				  (algt->mask & CRYPTO_ALG_INTERNAL));
	err = PTR_ERR(alg);
	if (IS_ERR(alg))
		goto err_free_inst;

	spawn = skcipher_instance_ctx(inst);
	err = crypto_init_spawn(spawn, alg, skcipher_crypto_instance(inst),
				CRYPTO_ALG_TYPE_MASK);
	crypto_mod_put(alg);
	if (err)
		goto err_free_inst;

	err = crypto_inst_setname(skcipher_crypto_instance(inst), "pcbc", alg);
	if (err)
		goto err_drop_spawn;

	inst->alg.base.cra_flags = alg->cra_flags & CRYPTO_ALG_INTERNAL;
	inst->alg.base.cra_priority = alg->cra_priority;
	inst->alg.base.cra_blocksize = alg->cra_blocksize;
	inst->alg.base.cra_alignmask = alg->cra_alignmask;

	/* We access the data as u32s when xoring. */
	inst->alg.base.cra_alignmask |= __alignof__(u32) - 1;

	inst->alg.ivsize = alg->cra_blocksize;
	inst->alg.min_keysize = alg->cra_cipher.cia_min_keysize;
	inst->alg.max_keysize = alg->cra_cipher.cia_max_keysize;

	inst->alg.base.cra_ctxsize = sizeof(struct crypto_pcbc_ctx);

	inst->alg.init = crypto_pcbc_init_tfm;
	inst->alg.exit = crypto_pcbc_exit_tfm;

	inst->alg.setkey = crypto_pcbc_setkey;
	inst->alg.encrypt = crypto_pcbc_encrypt;
	inst->alg.decrypt = crypto_pcbc_decrypt;

	inst->free = crypto_pcbc_free;

	err = skcipher_register_instance(tmpl, inst);
	if (err)
		goto err_drop_spawn;

out:
	return err;

err_drop_spawn:
	crypto_drop_spawn(spawn);
err_free_inst:
	kfree(inst);
	goto out;
}

static struct crypto_template crypto_pcbc_tmpl = {
	.name = "pcbc",
	.create = crypto_pcbc_create,
	.module = THIS_MODULE,
};

static int __init crypto_pcbc_module_init(void)
{
	return crypto_register_template(&crypto_pcbc_tmpl);
}

static void __exit crypto_pcbc_module_exit(void)
{
	crypto_unregister_template(&crypto_pcbc_tmpl);
}

module_init(crypto_pcbc_module_init);
module_exit(crypto_pcbc_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCBC block cipher algorithm");
MODULE_ALIAS_CRYPTO("pcbc");
