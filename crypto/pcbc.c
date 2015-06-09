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

#include <crypto/algapi.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

struct crypto_pcbc_ctx {
	struct crypto_cipher *child;
};

static int crypto_pcbc_setkey(struct crypto_tfm *parent, const u8 *key,
			      unsigned int keylen)
{
	struct crypto_pcbc_ctx *ctx = crypto_tfm_ctx(parent);
	struct crypto_cipher *child = ctx->child;
	int err;

	crypto_cipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_cipher_set_flags(child, crypto_tfm_get_flags(parent) &
				CRYPTO_TFM_REQ_MASK);
	err = crypto_cipher_setkey(child, key, keylen);
	crypto_tfm_set_flags(parent, crypto_cipher_get_flags(child) &
			     CRYPTO_TFM_RES_MASK);
	return err;
}

static int crypto_pcbc_encrypt_segment(struct blkcipher_desc *desc,
				       struct blkcipher_walk *walk,
				       struct crypto_cipher *tfm)
{
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) =
		crypto_cipher_alg(tfm)->cia_encrypt;
	int bsize = crypto_cipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	u8 *iv = walk->iv;

	do {
		crypto_xor(iv, src, bsize);
		fn(crypto_cipher_tfm(tfm), dst, iv);
		memcpy(iv, dst, bsize);
		crypto_xor(iv, src, bsize);

		src += bsize;
		dst += bsize;
	} while ((nbytes -= bsize) >= bsize);

	return nbytes;
}

static int crypto_pcbc_encrypt_inplace(struct blkcipher_desc *desc,
				       struct blkcipher_walk *walk,
				       struct crypto_cipher *tfm)
{
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) =
		crypto_cipher_alg(tfm)->cia_encrypt;
	int bsize = crypto_cipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *iv = walk->iv;
	u8 tmpbuf[bsize];

	do {
		memcpy(tmpbuf, src, bsize);
		crypto_xor(iv, src, bsize);
		fn(crypto_cipher_tfm(tfm), src, iv);
		memcpy(iv, tmpbuf, bsize);
		crypto_xor(iv, src, bsize);

		src += bsize;
	} while ((nbytes -= bsize) >= bsize);

	memcpy(walk->iv, iv, bsize);

	return nbytes;
}

static int crypto_pcbc_encrypt(struct blkcipher_desc *desc,
			       struct scatterlist *dst, struct scatterlist *src,
			       unsigned int nbytes)
{
	struct blkcipher_walk walk;
	struct crypto_blkcipher *tfm = desc->tfm;
	struct crypto_pcbc_ctx *ctx = crypto_blkcipher_ctx(tfm);
	struct crypto_cipher *child = ctx->child;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while ((nbytes = walk.nbytes)) {
		if (walk.src.virt.addr == walk.dst.virt.addr)
			nbytes = crypto_pcbc_encrypt_inplace(desc, &walk,
							     child);
		else
			nbytes = crypto_pcbc_encrypt_segment(desc, &walk,
							     child);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	return err;
}

static int crypto_pcbc_decrypt_segment(struct blkcipher_desc *desc,
				       struct blkcipher_walk *walk,
				       struct crypto_cipher *tfm)
{
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) =
		crypto_cipher_alg(tfm)->cia_decrypt;
	int bsize = crypto_cipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	u8 *iv = walk->iv;

	do {
		fn(crypto_cipher_tfm(tfm), dst, src);
		crypto_xor(dst, iv, bsize);
		memcpy(iv, src, bsize);
		crypto_xor(iv, dst, bsize);

		src += bsize;
		dst += bsize;
	} while ((nbytes -= bsize) >= bsize);

	memcpy(walk->iv, iv, bsize);

	return nbytes;
}

static int crypto_pcbc_decrypt_inplace(struct blkcipher_desc *desc,
				       struct blkcipher_walk *walk,
				       struct crypto_cipher *tfm)
{
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) =
		crypto_cipher_alg(tfm)->cia_decrypt;
	int bsize = crypto_cipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *iv = walk->iv;
	u8 tmpbuf[bsize];

	do {
		memcpy(tmpbuf, src, bsize);
		fn(crypto_cipher_tfm(tfm), src, src);
		crypto_xor(src, iv, bsize);
		memcpy(iv, tmpbuf, bsize);
		crypto_xor(iv, src, bsize);

		src += bsize;
	} while ((nbytes -= bsize) >= bsize);

	memcpy(walk->iv, iv, bsize);

	return nbytes;
}

static int crypto_pcbc_decrypt(struct blkcipher_desc *desc,
			       struct scatterlist *dst, struct scatterlist *src,
			       unsigned int nbytes)
{
	struct blkcipher_walk walk;
	struct crypto_blkcipher *tfm = desc->tfm;
	struct crypto_pcbc_ctx *ctx = crypto_blkcipher_ctx(tfm);
	struct crypto_cipher *child = ctx->child;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while ((nbytes = walk.nbytes)) {
		if (walk.src.virt.addr == walk.dst.virt.addr)
			nbytes = crypto_pcbc_decrypt_inplace(desc, &walk,
							     child);
		else
			nbytes = crypto_pcbc_decrypt_segment(desc, &walk,
							     child);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	return err;
}

static int crypto_pcbc_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_spawn *spawn = crypto_instance_ctx(inst);
	struct crypto_pcbc_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_cipher *cipher;

	cipher = crypto_spawn_cipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;
	return 0;
}

static void crypto_pcbc_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_pcbc_ctx *ctx = crypto_tfm_ctx(tfm);
	crypto_free_cipher(ctx->child);
}

static struct crypto_instance *crypto_pcbc_alloc(struct rtattr **tb)
{
	struct crypto_instance *inst;
	struct crypto_alg *alg;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_BLKCIPHER);
	if (err)
		return ERR_PTR(err);

	alg = crypto_get_attr_alg(tb, CRYPTO_ALG_TYPE_CIPHER,
				  CRYPTO_ALG_TYPE_MASK);
	if (IS_ERR(alg))
		return ERR_CAST(alg);

	inst = crypto_alloc_instance("pcbc", alg);
	if (IS_ERR(inst))
		goto out_put_alg;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_BLKCIPHER;
	inst->alg.cra_priority = alg->cra_priority;
	inst->alg.cra_blocksize = alg->cra_blocksize;
	inst->alg.cra_alignmask = alg->cra_alignmask;
	inst->alg.cra_type = &crypto_blkcipher_type;

	/* We access the data as u32s when xoring. */
	inst->alg.cra_alignmask |= __alignof__(u32) - 1;

	inst->alg.cra_blkcipher.ivsize = alg->cra_blocksize;
	inst->alg.cra_blkcipher.min_keysize = alg->cra_cipher.cia_min_keysize;
	inst->alg.cra_blkcipher.max_keysize = alg->cra_cipher.cia_max_keysize;

	inst->alg.cra_ctxsize = sizeof(struct crypto_pcbc_ctx);

	inst->alg.cra_init = crypto_pcbc_init_tfm;
	inst->alg.cra_exit = crypto_pcbc_exit_tfm;

	inst->alg.cra_blkcipher.setkey = crypto_pcbc_setkey;
	inst->alg.cra_blkcipher.encrypt = crypto_pcbc_encrypt;
	inst->alg.cra_blkcipher.decrypt = crypto_pcbc_decrypt;

out_put_alg:
	crypto_mod_put(alg);
	return inst;
}

static void crypto_pcbc_free(struct crypto_instance *inst)
{
	crypto_drop_spawn(crypto_instance_ctx(inst));
	kfree(inst);
}

static struct crypto_template crypto_pcbc_tmpl = {
	.name = "pcbc",
	.alloc = crypto_pcbc_alloc,
	.free = crypto_pcbc_free,
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
