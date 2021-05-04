// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CTR: Counter mode
 *
 * (C) Copyright IBM Corp. 2007 - Joy Latten <latten@us.ibm.com>
 */

#include <crypto/algapi.h>
#include <crypto/ctr.h>
#include <crypto/internal/cipher.h>
#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

struct crypto_rfc3686_ctx {
	struct crypto_skcipher *child;
	u8 nonce[CTR_RFC3686_NONCE_SIZE];
};

struct crypto_rfc3686_req_ctx {
	u8 iv[CTR_RFC3686_BLOCK_SIZE];
	struct skcipher_request subreq CRYPTO_MINALIGN_ATTR;
};

static void crypto_ctr_crypt_final(struct skcipher_walk *walk,
				   struct crypto_cipher *tfm)
{
	unsigned int bsize = crypto_cipher_blocksize(tfm);
	unsigned long alignmask = crypto_cipher_alignmask(tfm);
	u8 *ctrblk = walk->iv;
	u8 tmp[MAX_CIPHER_BLOCKSIZE + MAX_CIPHER_ALIGNMASK];
	u8 *keystream = PTR_ALIGN(tmp + 0, alignmask + 1);
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	unsigned int nbytes = walk->nbytes;

	crypto_cipher_encrypt_one(tfm, keystream, ctrblk);
	crypto_xor_cpy(dst, keystream, src, nbytes);

	crypto_inc(ctrblk, bsize);
}

static int crypto_ctr_crypt_segment(struct skcipher_walk *walk,
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

static int crypto_ctr_crypt_inplace(struct skcipher_walk *walk,
				    struct crypto_cipher *tfm)
{
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) =
		   crypto_cipher_alg(tfm)->cia_encrypt;
	unsigned int bsize = crypto_cipher_blocksize(tfm);
	unsigned long alignmask = crypto_cipher_alignmask(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *ctrblk = walk->iv;
	u8 *src = walk->src.virt.addr;
	u8 tmp[MAX_CIPHER_BLOCKSIZE + MAX_CIPHER_ALIGNMASK];
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

static int crypto_ctr_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_cipher *cipher = skcipher_cipher_simple(tfm);
	const unsigned int bsize = crypto_cipher_blocksize(cipher);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes >= bsize) {
		if (walk.src.virt.addr == walk.dst.virt.addr)
			nbytes = crypto_ctr_crypt_inplace(&walk, cipher);
		else
			nbytes = crypto_ctr_crypt_segment(&walk, cipher);

		err = skcipher_walk_done(&walk, nbytes);
	}

	if (walk.nbytes) {
		crypto_ctr_crypt_final(&walk, cipher);
		err = skcipher_walk_done(&walk, 0);
	}

	return err;
}

static int crypto_ctr_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct skcipher_instance *inst;
	struct crypto_alg *alg;
	int err;

	inst = skcipher_alloc_instance_simple(tmpl, tb);
	if (IS_ERR(inst))
		return PTR_ERR(inst);

	alg = skcipher_ialg_simple(inst);

	/* Block size must be >= 4 bytes. */
	err = -EINVAL;
	if (alg->cra_blocksize < 4)
		goto out_free_inst;

	/* If this is false we'd fail the alignment of crypto_inc. */
	if (alg->cra_blocksize % 4)
		goto out_free_inst;

	/* CTR mode is a stream cipher. */
	inst->alg.base.cra_blocksize = 1;

	/*
	 * To simplify the implementation, configure the skcipher walk to only
	 * give a partial block at the very end, never earlier.
	 */
	inst->alg.chunksize = alg->cra_blocksize;

	inst->alg.encrypt = crypto_ctr_crypt;
	inst->alg.decrypt = crypto_ctr_crypt;

	err = skcipher_register_instance(tmpl, inst);
	if (err) {
out_free_inst:
		inst->free(inst);
	}

	return err;
}

static int crypto_rfc3686_setkey(struct crypto_skcipher *parent,
				 const u8 *key, unsigned int keylen)
{
	struct crypto_rfc3686_ctx *ctx = crypto_skcipher_ctx(parent);
	struct crypto_skcipher *child = ctx->child;

	/* the nonce is stored in bytes at end of key */
	if (keylen < CTR_RFC3686_NONCE_SIZE)
		return -EINVAL;

	memcpy(ctx->nonce, key + (keylen - CTR_RFC3686_NONCE_SIZE),
	       CTR_RFC3686_NONCE_SIZE);

	keylen -= CTR_RFC3686_NONCE_SIZE;

	crypto_skcipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(child, crypto_skcipher_get_flags(parent) &
					 CRYPTO_TFM_REQ_MASK);
	return crypto_skcipher_setkey(child, key, keylen);
}

static int crypto_rfc3686_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_rfc3686_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *child = ctx->child;
	unsigned long align = crypto_skcipher_alignmask(tfm);
	struct crypto_rfc3686_req_ctx *rctx =
		(void *)PTR_ALIGN((u8 *)skcipher_request_ctx(req), align + 1);
	struct skcipher_request *subreq = &rctx->subreq;
	u8 *iv = rctx->iv;

	/* set up counter block */
	memcpy(iv, ctx->nonce, CTR_RFC3686_NONCE_SIZE);
	memcpy(iv + CTR_RFC3686_NONCE_SIZE, req->iv, CTR_RFC3686_IV_SIZE);

	/* initialize counter portion of counter block */
	*(__be32 *)(iv + CTR_RFC3686_NONCE_SIZE + CTR_RFC3686_IV_SIZE) =
		cpu_to_be32(1);

	skcipher_request_set_tfm(subreq, child);
	skcipher_request_set_callback(subreq, req->base.flags,
				      req->base.complete, req->base.data);
	skcipher_request_set_crypt(subreq, req->src, req->dst,
				   req->cryptlen, iv);

	return crypto_skcipher_encrypt(subreq);
}

static int crypto_rfc3686_init_tfm(struct crypto_skcipher *tfm)
{
	struct skcipher_instance *inst = skcipher_alg_instance(tfm);
	struct crypto_skcipher_spawn *spawn = skcipher_instance_ctx(inst);
	struct crypto_rfc3686_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *cipher;
	unsigned long align;
	unsigned int reqsize;

	cipher = crypto_spawn_skcipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;

	align = crypto_skcipher_alignmask(tfm);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	reqsize = align + sizeof(struct crypto_rfc3686_req_ctx) +
		  crypto_skcipher_reqsize(cipher);
	crypto_skcipher_set_reqsize(tfm, reqsize);

	return 0;
}

static void crypto_rfc3686_exit_tfm(struct crypto_skcipher *tfm)
{
	struct crypto_rfc3686_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_skcipher(ctx->child);
}

static void crypto_rfc3686_free(struct skcipher_instance *inst)
{
	struct crypto_skcipher_spawn *spawn = skcipher_instance_ctx(inst);

	crypto_drop_skcipher(spawn);
	kfree(inst);
}

static int crypto_rfc3686_create(struct crypto_template *tmpl,
				 struct rtattr **tb)
{
	struct skcipher_instance *inst;
	struct skcipher_alg *alg;
	struct crypto_skcipher_spawn *spawn;
	u32 mask;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SKCIPHER, &mask);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	spawn = skcipher_instance_ctx(inst);

	err = crypto_grab_skcipher(spawn, skcipher_crypto_instance(inst),
				   crypto_attr_alg_name(tb[1]), 0, mask);
	if (err)
		goto err_free_inst;

	alg = crypto_spawn_skcipher_alg(spawn);

	/* We only support 16-byte blocks. */
	err = -EINVAL;
	if (crypto_skcipher_alg_ivsize(alg) != CTR_RFC3686_BLOCK_SIZE)
		goto err_free_inst;

	/* Not a stream cipher? */
	if (alg->base.cra_blocksize != 1)
		goto err_free_inst;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
		     "rfc3686(%s)", alg->base.cra_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;
	if (snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "rfc3686(%s)", alg->base.cra_driver_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	inst->alg.base.cra_priority = alg->base.cra_priority;
	inst->alg.base.cra_blocksize = 1;
	inst->alg.base.cra_alignmask = alg->base.cra_alignmask;

	inst->alg.ivsize = CTR_RFC3686_IV_SIZE;
	inst->alg.chunksize = crypto_skcipher_alg_chunksize(alg);
	inst->alg.min_keysize = crypto_skcipher_alg_min_keysize(alg) +
				CTR_RFC3686_NONCE_SIZE;
	inst->alg.max_keysize = crypto_skcipher_alg_max_keysize(alg) +
				CTR_RFC3686_NONCE_SIZE;

	inst->alg.setkey = crypto_rfc3686_setkey;
	inst->alg.encrypt = crypto_rfc3686_crypt;
	inst->alg.decrypt = crypto_rfc3686_crypt;

	inst->alg.base.cra_ctxsize = sizeof(struct crypto_rfc3686_ctx);

	inst->alg.init = crypto_rfc3686_init_tfm;
	inst->alg.exit = crypto_rfc3686_exit_tfm;

	inst->free = crypto_rfc3686_free;

	err = skcipher_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		crypto_rfc3686_free(inst);
	}
	return err;
}

static struct crypto_template crypto_ctr_tmpls[] = {
	{
		.name = "ctr",
		.create = crypto_ctr_create,
		.module = THIS_MODULE,
	}, {
		.name = "rfc3686",
		.create = crypto_rfc3686_create,
		.module = THIS_MODULE,
	},
};

static int __init crypto_ctr_module_init(void)
{
	return crypto_register_templates(crypto_ctr_tmpls,
					 ARRAY_SIZE(crypto_ctr_tmpls));
}

static void __exit crypto_ctr_module_exit(void)
{
	crypto_unregister_templates(crypto_ctr_tmpls,
				    ARRAY_SIZE(crypto_ctr_tmpls));
}

subsys_initcall(crypto_ctr_module_init);
module_exit(crypto_ctr_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CTR block cipher mode of operation");
MODULE_ALIAS_CRYPTO("rfc3686");
MODULE_ALIAS_CRYPTO("ctr");
MODULE_IMPORT_NS(CRYPTO_INTERNAL);
