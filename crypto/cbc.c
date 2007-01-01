/*
 * CBC: Cipher Block Chaining mode
 *
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
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

struct crypto_cbc_ctx {
	struct crypto_cipher *child;
	void (*xor)(u8 *dst, const u8 *src, unsigned int bs);
};

static int crypto_cbc_setkey(struct crypto_tfm *parent, const u8 *key,
			     unsigned int keylen)
{
	struct crypto_cbc_ctx *ctx = crypto_tfm_ctx(parent);
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

static int crypto_cbc_encrypt_segment(struct blkcipher_desc *desc,
				      struct blkcipher_walk *walk,
				      struct crypto_cipher *tfm,
				      void (*xor)(u8 *, const u8 *,
						  unsigned int))
{
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) =
		crypto_cipher_alg(tfm)->cia_encrypt;
	int bsize = crypto_cipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	u8 *iv = walk->iv;

	do {
		xor(iv, src, bsize);
		fn(crypto_cipher_tfm(tfm), dst, iv);
		memcpy(iv, dst, bsize);

		src += bsize;
		dst += bsize;
	} while ((nbytes -= bsize) >= bsize);

	return nbytes;
}

static int crypto_cbc_encrypt_inplace(struct blkcipher_desc *desc,
				      struct blkcipher_walk *walk,
				      struct crypto_cipher *tfm,
				      void (*xor)(u8 *, const u8 *,
						  unsigned int))
{
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) =
		crypto_cipher_alg(tfm)->cia_encrypt;
	int bsize = crypto_cipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *iv = walk->iv;

	do {
		xor(src, iv, bsize);
		fn(crypto_cipher_tfm(tfm), src, src);
		iv = src;

		src += bsize;
	} while ((nbytes -= bsize) >= bsize);

	memcpy(walk->iv, iv, bsize);

	return nbytes;
}

static int crypto_cbc_encrypt(struct blkcipher_desc *desc,
			      struct scatterlist *dst, struct scatterlist *src,
			      unsigned int nbytes)
{
	struct blkcipher_walk walk;
	struct crypto_blkcipher *tfm = desc->tfm;
	struct crypto_cbc_ctx *ctx = crypto_blkcipher_ctx(tfm);
	struct crypto_cipher *child = ctx->child;
	void (*xor)(u8 *, const u8 *, unsigned int bs) = ctx->xor;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while ((nbytes = walk.nbytes)) {
		if (walk.src.virt.addr == walk.dst.virt.addr)
			nbytes = crypto_cbc_encrypt_inplace(desc, &walk, child,
							    xor);
		else
			nbytes = crypto_cbc_encrypt_segment(desc, &walk, child,
							    xor);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	return err;
}

static int crypto_cbc_decrypt_segment(struct blkcipher_desc *desc,
				      struct blkcipher_walk *walk,
				      struct crypto_cipher *tfm,
				      void (*xor)(u8 *, const u8 *,
						  unsigned int))
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
		xor(dst, iv, bsize);
		iv = src;

		src += bsize;
		dst += bsize;
	} while ((nbytes -= bsize) >= bsize);

	memcpy(walk->iv, iv, bsize);

	return nbytes;
}

static int crypto_cbc_decrypt_inplace(struct blkcipher_desc *desc,
				      struct blkcipher_walk *walk,
				      struct crypto_cipher *tfm,
				      void (*xor)(u8 *, const u8 *,
						  unsigned int))
{
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) =
		crypto_cipher_alg(tfm)->cia_decrypt;
	int bsize = crypto_cipher_blocksize(tfm);
	unsigned long alignmask = crypto_cipher_alignmask(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 stack[bsize + alignmask];
	u8 *first_iv = (u8 *)ALIGN((unsigned long)stack, alignmask + 1);

	memcpy(first_iv, walk->iv, bsize);

	/* Start of the last block. */
	src += nbytes - nbytes % bsize - bsize;
	memcpy(walk->iv, src, bsize);

	for (;;) {
		fn(crypto_cipher_tfm(tfm), src, src);
		if ((nbytes -= bsize) < bsize)
			break;
		xor(src, src - bsize, bsize);
		src -= bsize;
	}

	xor(src, first_iv, bsize);

	return nbytes;
}

static int crypto_cbc_decrypt(struct blkcipher_desc *desc,
			      struct scatterlist *dst, struct scatterlist *src,
			      unsigned int nbytes)
{
	struct blkcipher_walk walk;
	struct crypto_blkcipher *tfm = desc->tfm;
	struct crypto_cbc_ctx *ctx = crypto_blkcipher_ctx(tfm);
	struct crypto_cipher *child = ctx->child;
	void (*xor)(u8 *, const u8 *, unsigned int bs) = ctx->xor;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while ((nbytes = walk.nbytes)) {
		if (walk.src.virt.addr == walk.dst.virt.addr)
			nbytes = crypto_cbc_decrypt_inplace(desc, &walk, child,
							    xor);
		else
			nbytes = crypto_cbc_decrypt_segment(desc, &walk, child,
							    xor);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	return err;
}

static void xor_byte(u8 *a, const u8 *b, unsigned int bs)
{
	do {
		*a++ ^= *b++;
	} while (--bs);
}

static void xor_quad(u8 *dst, const u8 *src, unsigned int bs)
{
	u32 *a = (u32 *)dst;
	u32 *b = (u32 *)src;

	do {
		*a++ ^= *b++;
	} while ((bs -= 4));
}

static void xor_64(u8 *a, const u8 *b, unsigned int bs)
{
	((u32 *)a)[0] ^= ((u32 *)b)[0];
	((u32 *)a)[1] ^= ((u32 *)b)[1];
}

static void xor_128(u8 *a, const u8 *b, unsigned int bs)
{
	((u32 *)a)[0] ^= ((u32 *)b)[0];
	((u32 *)a)[1] ^= ((u32 *)b)[1];
	((u32 *)a)[2] ^= ((u32 *)b)[2];
	((u32 *)a)[3] ^= ((u32 *)b)[3];
}

static int crypto_cbc_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_spawn *spawn = crypto_instance_ctx(inst);
	struct crypto_cbc_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_cipher *cipher;

	switch (crypto_tfm_alg_blocksize(tfm)) {
	case 8:
		ctx->xor = xor_64;
		break;

	case 16:
		ctx->xor = xor_128;
		break;

	default:
		if (crypto_tfm_alg_blocksize(tfm) % 4)
			ctx->xor = xor_byte;
		else
			ctx->xor = xor_quad;
	}

	cipher = crypto_spawn_cipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;
	return 0;
}

static void crypto_cbc_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_cbc_ctx *ctx = crypto_tfm_ctx(tfm);
	crypto_free_cipher(ctx->child);
}

static struct crypto_instance *crypto_cbc_alloc(struct rtattr **tb)
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
		return ERR_PTR(PTR_ERR(alg));

	inst = crypto_alloc_instance("cbc", alg);
	if (IS_ERR(inst))
		goto out_put_alg;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_BLKCIPHER;
	inst->alg.cra_priority = alg->cra_priority;
	inst->alg.cra_blocksize = alg->cra_blocksize;
	inst->alg.cra_alignmask = alg->cra_alignmask;
	inst->alg.cra_type = &crypto_blkcipher_type;

	if (!(alg->cra_blocksize % 4))
		inst->alg.cra_alignmask |= 3;
	inst->alg.cra_blkcipher.ivsize = alg->cra_blocksize;
	inst->alg.cra_blkcipher.min_keysize = alg->cra_cipher.cia_min_keysize;
	inst->alg.cra_blkcipher.max_keysize = alg->cra_cipher.cia_max_keysize;

	inst->alg.cra_ctxsize = sizeof(struct crypto_cbc_ctx);

	inst->alg.cra_init = crypto_cbc_init_tfm;
	inst->alg.cra_exit = crypto_cbc_exit_tfm;

	inst->alg.cra_blkcipher.setkey = crypto_cbc_setkey;
	inst->alg.cra_blkcipher.encrypt = crypto_cbc_encrypt;
	inst->alg.cra_blkcipher.decrypt = crypto_cbc_decrypt;

out_put_alg:
	crypto_mod_put(alg);
	return inst;
}

static void crypto_cbc_free(struct crypto_instance *inst)
{
	crypto_drop_spawn(crypto_instance_ctx(inst));
	kfree(inst);
}

static struct crypto_template crypto_cbc_tmpl = {
	.name = "cbc",
	.alloc = crypto_cbc_alloc,
	.free = crypto_cbc_free,
	.module = THIS_MODULE,
};

static int __init crypto_cbc_module_init(void)
{
	return crypto_register_template(&crypto_cbc_tmpl);
}

static void __exit crypto_cbc_module_exit(void)
{
	crypto_unregister_template(&crypto_cbc_tmpl);
}

module_init(crypto_cbc_module_init);
module_exit(crypto_cbc_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CBC block cipher algorithm");
