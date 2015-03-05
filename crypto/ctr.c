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
#include <crypto/ctr.h>
#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

struct crypto_ctr_ctx {
	struct crypto_cipher *child;
};

struct crypto_rfc3686_ctx {
	struct crypto_ablkcipher *child;
	u8 nonce[CTR_RFC3686_NONCE_SIZE];
};

struct crypto_rfc3686_req_ctx {
	u8 iv[CTR_RFC3686_BLOCK_SIZE];
	struct ablkcipher_request subreq CRYPTO_MINALIGN_ATTR;
};

static int crypto_ctr_setkey(struct crypto_tfm *parent, const u8 *key,
			     unsigned int keylen)
{
	struct crypto_ctr_ctx *ctx = crypto_tfm_ctx(parent);
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

static void crypto_ctr_crypt_final(struct blkcipher_walk *walk,
				   struct crypto_cipher *tfm)
{
	unsigned int bsize = crypto_cipher_blocksize(tfm);
	unsigned long alignmask = crypto_cipher_alignmask(tfm);
	u8 *ctrblk = walk->iv;
	u8 tmp[bsize + alignmask];
	u8 *keystream = PTR_ALIGN(tmp + 0, alignmask + 1);
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	unsigned int nbytes = walk->nbytes;

	crypto_cipher_encrypt_one(tfm, keystream, ctrblk);
	crypto_xor(keystream, src, nbytes);
	memcpy(dst, keystream, nbytes);

	crypto_inc(ctrblk, bsize);
}

static int crypto_ctr_crypt_segment(struct blkcipher_walk *walk,
				    struct crypto_cipher *tfm)
{
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) =
		   crypto_cipher_alg(tfm)->cia_encrypt;
	unsigned int bsize = crypto_cipher_blocksize(tfm);
	u8 *ctrblk = walk->iv;
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	unsigned int nbytes = walk->nbytes;

	do {
		/* create keystream */
		fn(crypto_cipher_tfm(tfm), dst, ctrblk);
		crypto_xor(dst, src, bsize);

		/* increment counter in counterblock */
		crypto_inc(ctrblk, bsize);

		src += bsize;
		dst += bsize;
	} while ((nbytes -= bsize) >= bsize);

	return nbytes;
}

static int crypto_ctr_crypt_inplace(struct blkcipher_walk *walk,
				    struct crypto_cipher *tfm)
{
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) =
		   crypto_cipher_alg(tfm)->cia_encrypt;
	unsigned int bsize = crypto_cipher_blocksize(tfm);
	unsigned long alignmask = crypto_cipher_alignmask(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *ctrblk = walk->iv;
	u8 *src = walk->src.virt.addr;
	u8 tmp[bsize + alignmask];
	u8 *keystream = PTR_ALIGN(tmp + 0, alignmask + 1);

	do {
		/* create keystream */
		fn(crypto_cipher_tfm(tfm), keystream, ctrblk);
		crypto_xor(src, keystream, bsize);

		/* increment counter in counterblock */
		crypto_inc(ctrblk, bsize);

		src += bsize;
	} while ((nbytes -= bsize) >= bsize);

	return nbytes;
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
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt_block(desc, &walk, bsize);

	while (walk.nbytes >= bsize) {
		if (walk.src.virt.addr == walk.dst.virt.addr)
			nbytes = crypto_ctr_crypt_inplace(&walk, child);
		else
			nbytes = crypto_ctr_crypt_segment(&walk, child);

		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	if (walk.nbytes) {
		crypto_ctr_crypt_final(&walk, child);
		err = blkcipher_walk_done(desc, &walk, 0);
	}

	return err;
}

static int crypto_ctr_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_spawn *spawn = crypto_instance_ctx(inst);
	struct crypto_ctr_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_cipher *cipher;

	cipher = crypto_spawn_cipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;

	return 0;
}

static void crypto_ctr_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_ctr_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_cipher(ctx->child);
}

static struct crypto_instance *crypto_ctr_alloc(struct rtattr **tb)
{
	struct crypto_instance *inst;
	struct crypto_alg *alg;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_BLKCIPHER);
	if (err)
		return ERR_PTR(err);

	alg = crypto_attr_alg(tb[1], CRYPTO_ALG_TYPE_CIPHER,
				  CRYPTO_ALG_TYPE_MASK);
	if (IS_ERR(alg))
		return ERR_CAST(alg);

	/* Block size must be >= 4 bytes. */
	err = -EINVAL;
	if (alg->cra_blocksize < 4)
		goto out_put_alg;

	/* If this is false we'd fail the alignment of crypto_inc. */
	if (alg->cra_blocksize % 4)
		goto out_put_alg;

	inst = crypto_alloc_instance("ctr", alg);
	if (IS_ERR(inst))
		goto out;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_BLKCIPHER;
	inst->alg.cra_priority = alg->cra_priority;
	inst->alg.cra_blocksize = 1;
	inst->alg.cra_alignmask = alg->cra_alignmask | (__alignof__(u32) - 1);
	inst->alg.cra_type = &crypto_blkcipher_type;

	inst->alg.cra_blkcipher.ivsize = alg->cra_blocksize;
	inst->alg.cra_blkcipher.min_keysize = alg->cra_cipher.cia_min_keysize;
	inst->alg.cra_blkcipher.max_keysize = alg->cra_cipher.cia_max_keysize;

	inst->alg.cra_ctxsize = sizeof(struct crypto_ctr_ctx);

	inst->alg.cra_init = crypto_ctr_init_tfm;
	inst->alg.cra_exit = crypto_ctr_exit_tfm;

	inst->alg.cra_blkcipher.setkey = crypto_ctr_setkey;
	inst->alg.cra_blkcipher.encrypt = crypto_ctr_crypt;
	inst->alg.cra_blkcipher.decrypt = crypto_ctr_crypt;

	inst->alg.cra_blkcipher.geniv = "chainiv";

out:
	crypto_mod_put(alg);
	return inst;

out_put_alg:
	inst = ERR_PTR(err);
	goto out;
}

static void crypto_ctr_free(struct crypto_instance *inst)
{
	crypto_drop_spawn(crypto_instance_ctx(inst));
	kfree(inst);
}

static struct crypto_template crypto_ctr_tmpl = {
	.name = "ctr",
	.alloc = crypto_ctr_alloc,
	.free = crypto_ctr_free,
	.module = THIS_MODULE,
};

static int crypto_rfc3686_setkey(struct crypto_ablkcipher *parent,
				 const u8 *key, unsigned int keylen)
{
	struct crypto_rfc3686_ctx *ctx = crypto_ablkcipher_ctx(parent);
	struct crypto_ablkcipher *child = ctx->child;
	int err;

	/* the nonce is stored in bytes at end of key */
	if (keylen < CTR_RFC3686_NONCE_SIZE)
		return -EINVAL;

	memcpy(ctx->nonce, key + (keylen - CTR_RFC3686_NONCE_SIZE),
	       CTR_RFC3686_NONCE_SIZE);

	keylen -= CTR_RFC3686_NONCE_SIZE;

	crypto_ablkcipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_ablkcipher_set_flags(child, crypto_ablkcipher_get_flags(parent) &
				    CRYPTO_TFM_REQ_MASK);
	err = crypto_ablkcipher_setkey(child, key, keylen);
	crypto_ablkcipher_set_flags(parent, crypto_ablkcipher_get_flags(child) &
				    CRYPTO_TFM_RES_MASK);

	return err;
}

static int crypto_rfc3686_crypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct crypto_rfc3686_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct crypto_ablkcipher *child = ctx->child;
	unsigned long align = crypto_ablkcipher_alignmask(tfm);
	struct crypto_rfc3686_req_ctx *rctx =
		(void *)PTR_ALIGN((u8 *)ablkcipher_request_ctx(req), align + 1);
	struct ablkcipher_request *subreq = &rctx->subreq;
	u8 *iv = rctx->iv;

	/* set up counter block */
	memcpy(iv, ctx->nonce, CTR_RFC3686_NONCE_SIZE);
	memcpy(iv + CTR_RFC3686_NONCE_SIZE, req->info, CTR_RFC3686_IV_SIZE);

	/* initialize counter portion of counter block */
	*(__be32 *)(iv + CTR_RFC3686_NONCE_SIZE + CTR_RFC3686_IV_SIZE) =
		cpu_to_be32(1);

	ablkcipher_request_set_tfm(subreq, child);
	ablkcipher_request_set_callback(subreq, req->base.flags,
					req->base.complete, req->base.data);
	ablkcipher_request_set_crypt(subreq, req->src, req->dst, req->nbytes,
				     iv);

	return crypto_ablkcipher_encrypt(subreq);
}

static int crypto_rfc3686_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_skcipher_spawn *spawn = crypto_instance_ctx(inst);
	struct crypto_rfc3686_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_ablkcipher *cipher;
	unsigned long align;

	cipher = crypto_spawn_skcipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;

	align = crypto_tfm_alg_alignmask(tfm);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	tfm->crt_ablkcipher.reqsize = align +
		sizeof(struct crypto_rfc3686_req_ctx) +
		crypto_ablkcipher_reqsize(cipher);

	return 0;
}

static void crypto_rfc3686_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_rfc3686_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_ablkcipher(ctx->child);
}

static struct crypto_instance *crypto_rfc3686_alloc(struct rtattr **tb)
{
	struct crypto_attr_type *algt;
	struct crypto_instance *inst;
	struct crypto_alg *alg;
	struct crypto_skcipher_spawn *spawn;
	const char *cipher_name;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return ERR_CAST(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_BLKCIPHER) & algt->mask)
		return ERR_PTR(-EINVAL);

	cipher_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(cipher_name))
		return ERR_CAST(cipher_name);

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return ERR_PTR(-ENOMEM);

	spawn = crypto_instance_ctx(inst);

	crypto_set_skcipher_spawn(spawn, inst);
	err = crypto_grab_skcipher(spawn, cipher_name, 0,
				   crypto_requires_sync(algt->type,
							algt->mask));
	if (err)
		goto err_free_inst;

	alg = crypto_skcipher_spawn_alg(spawn);

	/* We only support 16-byte blocks. */
	err = -EINVAL;
	if (alg->cra_ablkcipher.ivsize != CTR_RFC3686_BLOCK_SIZE)
		goto err_drop_spawn;

	/* Not a stream cipher? */
	if (alg->cra_blocksize != 1)
		goto err_drop_spawn;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_name, CRYPTO_MAX_ALG_NAME, "rfc3686(%s)",
		     alg->cra_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_drop_spawn;
	if (snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "rfc3686(%s)", alg->cra_driver_name) >=
			CRYPTO_MAX_ALG_NAME)
		goto err_drop_spawn;

	inst->alg.cra_priority = alg->cra_priority;
	inst->alg.cra_blocksize = 1;
	inst->alg.cra_alignmask = alg->cra_alignmask;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
			      (alg->cra_flags & CRYPTO_ALG_ASYNC);
	inst->alg.cra_type = &crypto_ablkcipher_type;

	inst->alg.cra_ablkcipher.ivsize = CTR_RFC3686_IV_SIZE;
	inst->alg.cra_ablkcipher.min_keysize =
		alg->cra_ablkcipher.min_keysize + CTR_RFC3686_NONCE_SIZE;
	inst->alg.cra_ablkcipher.max_keysize =
		alg->cra_ablkcipher.max_keysize + CTR_RFC3686_NONCE_SIZE;

	inst->alg.cra_ablkcipher.geniv = "seqiv";

	inst->alg.cra_ablkcipher.setkey = crypto_rfc3686_setkey;
	inst->alg.cra_ablkcipher.encrypt = crypto_rfc3686_crypt;
	inst->alg.cra_ablkcipher.decrypt = crypto_rfc3686_crypt;

	inst->alg.cra_ctxsize = sizeof(struct crypto_rfc3686_ctx);

	inst->alg.cra_init = crypto_rfc3686_init_tfm;
	inst->alg.cra_exit = crypto_rfc3686_exit_tfm;

	return inst;

err_drop_spawn:
	crypto_drop_skcipher(spawn);
err_free_inst:
	kfree(inst);
	return ERR_PTR(err);
}

static void crypto_rfc3686_free(struct crypto_instance *inst)
{
	struct crypto_skcipher_spawn *spawn = crypto_instance_ctx(inst);

	crypto_drop_skcipher(spawn);
	kfree(inst);
}

static struct crypto_template crypto_rfc3686_tmpl = {
	.name = "rfc3686",
	.alloc = crypto_rfc3686_alloc,
	.free = crypto_rfc3686_free,
	.module = THIS_MODULE,
};

static int __init crypto_ctr_module_init(void)
{
	int err;

	err = crypto_register_template(&crypto_ctr_tmpl);
	if (err)
		goto out;

	err = crypto_register_template(&crypto_rfc3686_tmpl);
	if (err)
		goto out_drop_ctr;

out:
	return err;

out_drop_ctr:
	crypto_unregister_template(&crypto_ctr_tmpl);
	goto out;
}

static void __exit crypto_ctr_module_exit(void)
{
	crypto_unregister_template(&crypto_rfc3686_tmpl);
	crypto_unregister_template(&crypto_ctr_tmpl);
}

module_init(crypto_ctr_module_init);
module_exit(crypto_ctr_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CTR Counter block mode");
MODULE_ALIAS_CRYPTO("rfc3686");
MODULE_ALIAS_CRYPTO("ctr");
