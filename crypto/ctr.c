/*
 * CTR: Counter mode
 *
 * (C) Copyright IBM Corp. 2007 - Joy Latten <latten@us.ibm.com>
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
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

struct ctr_instance_ctx {
	struct crypto_spawn alg;
	unsigned int noncesize;
	unsigned int ivsize;
};

struct crypto_ctr_ctx {
	struct crypto_cipher *child;
	u8 *nonce;
};

static inline void __ctr_inc_byte(u8 *a, unsigned int size)
{
	u8 *b = (a + size);
	u8 c;

	for (; size; size--) {
		c = *--b + 1;
		*b = c;
		if (c)
			break;
	}
}

static void ctr_inc_quad(u8 *a, unsigned int size)
{
	__be32 *b = (__be32 *)(a + size);
	u32 c;

	for (; size >= 4; size -=4) {
		c = be32_to_cpu(*--b) + 1;
		*b = cpu_to_be32(c);
		if (c)
			return;
	}

	__ctr_inc_byte(a, size);
}

static void xor_byte(u8 *a, const u8 *b, unsigned int bs)
{
	for (; bs; bs--)
		*a++ ^= *b++;
}

static void xor_quad(u8 *dst, const u8 *src, unsigned int bs)
{
	u32 *a = (u32 *)dst;
	u32 *b = (u32 *)src;

	for (; bs >= 4; bs -= 4)
		*a++ ^= *b++;

	xor_byte((u8 *)a, (u8 *)b, bs);
}

static int crypto_ctr_setkey(struct crypto_tfm *parent, const u8 *key,
			     unsigned int keylen)
{
	struct crypto_ctr_ctx *ctx = crypto_tfm_ctx(parent);
	struct crypto_cipher *child = ctx->child;
	struct ctr_instance_ctx *ictx =
		crypto_instance_ctx(crypto_tfm_alg_instance(parent));
	unsigned int noncelen = ictx->noncesize;
	int err = 0;

	/* the nonce is stored in bytes at end of key */
	if (keylen < noncelen)
		return  -EINVAL;

	memcpy(ctx->nonce, key + (keylen - noncelen), noncelen);

	keylen -=  noncelen;

	crypto_cipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_cipher_set_flags(child, crypto_tfm_get_flags(parent) &
				CRYPTO_TFM_REQ_MASK);
	err = crypto_cipher_setkey(child, key, keylen);
	crypto_tfm_set_flags(parent, crypto_cipher_get_flags(child) &
			     CRYPTO_TFM_RES_MASK);

	return err;
}

static int crypto_ctr_crypt_segment(struct blkcipher_walk *walk,
				    struct crypto_cipher *tfm, u8 *ctrblk,
				    unsigned int countersize)
{
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) =
		   crypto_cipher_alg(tfm)->cia_encrypt;
	unsigned int bsize = crypto_cipher_blocksize(tfm);
	unsigned long alignmask = crypto_cipher_alignmask(tfm);
	u8 ks[bsize + alignmask];
	u8 *keystream = (u8 *)ALIGN((unsigned long)ks, alignmask + 1);
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	unsigned int nbytes = walk->nbytes;

	do {
		/* create keystream */
		fn(crypto_cipher_tfm(tfm), keystream, ctrblk);
		xor_quad(keystream, src, min(nbytes, bsize));

		/* copy result into dst */
		memcpy(dst, keystream, min(nbytes, bsize));

		/* increment counter in counterblock */
		ctr_inc_quad(ctrblk + (bsize - countersize), countersize);

		if (nbytes < bsize)
			break;

		src += bsize;
		dst += bsize;
		nbytes -= bsize;

	} while (nbytes);

	return 0;
}

static int crypto_ctr_crypt_inplace(struct blkcipher_walk *walk,
				    struct crypto_cipher *tfm, u8 *ctrblk,
				    unsigned int countersize)
{
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) =
		   crypto_cipher_alg(tfm)->cia_encrypt;
	unsigned int bsize = crypto_cipher_blocksize(tfm);
	unsigned long alignmask = crypto_cipher_alignmask(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 ks[bsize + alignmask];
	u8 *keystream = (u8 *)ALIGN((unsigned long)ks, alignmask + 1);

	do {
		/* create keystream */
		fn(crypto_cipher_tfm(tfm), keystream, ctrblk);
		xor_quad(src, keystream, min(nbytes, bsize));

		/* increment counter in counterblock */
		ctr_inc_quad(ctrblk + (bsize - countersize), countersize);

		if (nbytes < bsize)
			break;

		src += bsize;
		nbytes -= bsize;

	} while (nbytes);

	return 0;
}

static int crypto_ctr_crypt(struct blkcipher_desc *desc,
			      struct scatterlist *dst, struct scatterlist *src,
			      unsigned int nbytes)
{
	struct blkcipher_walk walk;
	struct crypto_blkcipher *tfm = desc->tfm;
	struct crypto_ctr_ctx *ctx = crypto_blkcipher_ctx(tfm);
	struct crypto_cipher *child = ctx->child;
	unsigned int bsize = crypto_cipher_blocksize(child);
	struct ctr_instance_ctx *ictx =
		crypto_instance_ctx(crypto_tfm_alg_instance(&tfm->base));
	unsigned long alignmask = crypto_cipher_alignmask(child);
	u8 cblk[bsize + alignmask];
	u8 *counterblk = (u8 *)ALIGN((unsigned long)cblk, alignmask + 1);
	unsigned int countersize;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt_block(desc, &walk, bsize);

	/* set up counter block */
	memset(counterblk, 0 , bsize);
	memcpy(counterblk, ctx->nonce, ictx->noncesize);
	memcpy(counterblk + ictx->noncesize, walk.iv, ictx->ivsize);

	/* initialize counter portion of counter block */
	countersize = bsize - ictx->noncesize - ictx->ivsize;
	ctr_inc_quad(counterblk + (bsize - countersize), countersize);

	while (walk.nbytes) {
		if (walk.src.virt.addr == walk.dst.virt.addr)
			nbytes = crypto_ctr_crypt_inplace(&walk, child,
							  counterblk,
							  countersize);
		else
			nbytes = crypto_ctr_crypt_segment(&walk, child,
							  counterblk,
							  countersize);

		err = blkcipher_walk_done(desc, &walk, nbytes);
	}
	return err;
}

static int crypto_ctr_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct ctr_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct crypto_ctr_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_cipher *cipher;

	ctx->nonce = kzalloc(ictx->noncesize, GFP_KERNEL);
	if (!ctx->nonce)
		return -ENOMEM;

	cipher = crypto_spawn_cipher(&ictx->alg);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;

	return 0;
}

static void crypto_ctr_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_ctr_ctx *ctx = crypto_tfm_ctx(tfm);

	kfree(ctx->nonce);
	crypto_free_cipher(ctx->child);
}

static struct crypto_instance *crypto_ctr_alloc(struct rtattr **tb)
{
	struct crypto_instance *inst;
	struct crypto_alg *alg;
	struct ctr_instance_ctx *ictx;
	unsigned int noncesize;
	unsigned int ivsize;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_BLKCIPHER);
	if (err)
		return ERR_PTR(err);

	alg = crypto_attr_alg(tb[1], CRYPTO_ALG_TYPE_CIPHER,
				  CRYPTO_ALG_TYPE_MASK);
	if (IS_ERR(alg))
		return ERR_PTR(PTR_ERR(alg));

	err = crypto_attr_u32(tb[2], &noncesize);
	if (err)
		goto out_put_alg;

	err = crypto_attr_u32(tb[3], &ivsize);
	if (err)
		goto out_put_alg;

	/* verify size of nonce + iv + counter */
	err = -EINVAL;
	if ((noncesize + ivsize) >= alg->cra_blocksize)
		goto out_put_alg;

	inst = kzalloc(sizeof(*inst) + sizeof(*ictx), GFP_KERNEL);
	err = -ENOMEM;
	if (!inst)
		goto out_put_alg;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_name, CRYPTO_MAX_ALG_NAME,
		     "ctr(%s,%u,%u)", alg->cra_name, noncesize,
		     ivsize) >= CRYPTO_MAX_ALG_NAME) {
		goto err_free_inst;
	}

	if (snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "ctr(%s,%u,%u)", alg->cra_driver_name, noncesize,
		     ivsize) >= CRYPTO_MAX_ALG_NAME) {
		goto err_free_inst;
	}

	ictx = crypto_instance_ctx(inst);
	ictx->noncesize = noncesize;
	ictx->ivsize = ivsize;

	err = crypto_init_spawn(&ictx->alg, alg, inst,
		CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_ASYNC);
	if (err)
		goto err_free_inst;

	err = 0;
	inst->alg.cra_flags = CRYPTO_ALG_TYPE_BLKCIPHER;
	inst->alg.cra_priority = alg->cra_priority;
	inst->alg.cra_blocksize = 1;
	inst->alg.cra_alignmask = 3;
	inst->alg.cra_type = &crypto_blkcipher_type;

	inst->alg.cra_blkcipher.ivsize = ivsize;
	inst->alg.cra_blkcipher.min_keysize = alg->cra_cipher.cia_min_keysize
					      + noncesize;
	inst->alg.cra_blkcipher.max_keysize = alg->cra_cipher.cia_max_keysize
					      + noncesize;

	inst->alg.cra_ctxsize = sizeof(struct crypto_ctr_ctx);

	inst->alg.cra_init = crypto_ctr_init_tfm;
	inst->alg.cra_exit = crypto_ctr_exit_tfm;

	inst->alg.cra_blkcipher.setkey = crypto_ctr_setkey;
	inst->alg.cra_blkcipher.encrypt = crypto_ctr_crypt;
	inst->alg.cra_blkcipher.decrypt = crypto_ctr_crypt;

err_free_inst:
	if (err)
		kfree(inst);

out_put_alg:
	crypto_mod_put(alg);

	if (err)
		inst = ERR_PTR(err);

	return inst;
}

static void crypto_ctr_free(struct crypto_instance *inst)
{
	struct ctr_instance_ctx *ictx = crypto_instance_ctx(inst);

	crypto_drop_spawn(&ictx->alg);
	kfree(inst);
}

static struct crypto_template crypto_ctr_tmpl = {
	.name = "ctr",
	.alloc = crypto_ctr_alloc,
	.free = crypto_ctr_free,
	.module = THIS_MODULE,
};

static int __init crypto_ctr_module_init(void)
{
	return crypto_register_template(&crypto_ctr_tmpl);
}

static void __exit crypto_ctr_module_exit(void)
{
	crypto_unregister_template(&crypto_ctr_tmpl);
}

module_init(crypto_ctr_module_init);
module_exit(crypto_ctr_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CTR Counter block mode");
