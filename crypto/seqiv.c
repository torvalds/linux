/*
 * seqiv: Sequence Number IV Generator
 *
 * This generator generates an IV based on a sequence number by xoring it
 * with a salt.  This algorithm is mainly useful for CTR and similar modes.
 *
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/geniv.h>
#include <crypto/internal/skcipher.h>
#include <crypto/null.h>
#include <crypto/rng.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

struct seqniv_request_ctx {
	struct scatterlist dst[2];
	struct aead_request subreq;
};

struct seqiv_ctx {
	spinlock_t lock;
	u8 salt[] __attribute__ ((aligned(__alignof__(u32))));
};

struct seqiv_aead_ctx {
	/* aead_geniv_ctx must be first the element */
	struct aead_geniv_ctx geniv;
	struct crypto_blkcipher *null;
	u8 salt[] __attribute__ ((aligned(__alignof__(u32))));
};

static void seqiv_free(struct crypto_instance *inst);

static void seqiv_complete2(struct skcipher_givcrypt_request *req, int err)
{
	struct ablkcipher_request *subreq = skcipher_givcrypt_reqctx(req);
	struct crypto_ablkcipher *geniv;

	if (err == -EINPROGRESS)
		return;

	if (err)
		goto out;

	geniv = skcipher_givcrypt_reqtfm(req);
	memcpy(req->creq.info, subreq->info, crypto_ablkcipher_ivsize(geniv));

out:
	kfree(subreq->info);
}

static void seqiv_complete(struct crypto_async_request *base, int err)
{
	struct skcipher_givcrypt_request *req = base->data;

	seqiv_complete2(req, err);
	skcipher_givcrypt_complete(req, err);
}

static void seqiv_aead_complete2(struct aead_givcrypt_request *req, int err)
{
	struct aead_request *subreq = aead_givcrypt_reqctx(req);
	struct crypto_aead *geniv;

	if (err == -EINPROGRESS)
		return;

	if (err)
		goto out;

	geniv = aead_givcrypt_reqtfm(req);
	memcpy(req->areq.iv, subreq->iv, crypto_aead_ivsize(geniv));

out:
	kfree(subreq->iv);
}

static void seqiv_aead_complete(struct crypto_async_request *base, int err)
{
	struct aead_givcrypt_request *req = base->data;

	seqiv_aead_complete2(req, err);
	aead_givcrypt_complete(req, err);
}

static void seqiv_aead_encrypt_complete2(struct aead_request *req, int err)
{
	struct aead_request *subreq = aead_request_ctx(req);
	struct crypto_aead *geniv;

	if (err == -EINPROGRESS)
		return;

	if (err)
		goto out;

	geniv = crypto_aead_reqtfm(req);
	memcpy(req->iv, subreq->iv, crypto_aead_ivsize(geniv));

out:
	kzfree(subreq->iv);
}

static void seqiv_aead_encrypt_complete(struct crypto_async_request *base,
					int err)
{
	struct aead_request *req = base->data;

	seqiv_aead_encrypt_complete2(req, err);
	aead_request_complete(req, err);
}

static void seqniv_aead_encrypt_complete2(struct aead_request *req, int err)
{
	unsigned int ivsize = 8;
	u8 data[20];

	if (err == -EINPROGRESS)
		return;

	/* Swap IV and ESP header back to correct order. */
	scatterwalk_map_and_copy(data, req->dst, 0, req->assoclen + ivsize, 0);
	scatterwalk_map_and_copy(data + ivsize, req->dst, 0, req->assoclen, 1);
	scatterwalk_map_and_copy(data, req->dst, req->assoclen, ivsize, 1);
}

static void seqniv_aead_encrypt_complete(struct crypto_async_request *base,
					int err)
{
	struct aead_request *req = base->data;

	seqniv_aead_encrypt_complete2(req, err);
	aead_request_complete(req, err);
}

static void seqniv_aead_decrypt_complete2(struct aead_request *req, int err)
{
	u8 data[4];

	if (err == -EINPROGRESS)
		return;

	/* Move ESP header back to correct location. */
	scatterwalk_map_and_copy(data, req->dst, 16, req->assoclen - 8, 0);
	scatterwalk_map_and_copy(data, req->dst, 8, req->assoclen - 8, 1);
}

static void seqniv_aead_decrypt_complete(struct crypto_async_request *base,
					 int err)
{
	struct aead_request *req = base->data;

	seqniv_aead_decrypt_complete2(req, err);
	aead_request_complete(req, err);
}

static void seqiv_geniv(struct seqiv_ctx *ctx, u8 *info, u64 seq,
			unsigned int ivsize)
{
	unsigned int len = ivsize;

	if (ivsize > sizeof(u64)) {
		memset(info, 0, ivsize - sizeof(u64));
		len = sizeof(u64);
	}
	seq = cpu_to_be64(seq);
	memcpy(info + ivsize - len, &seq, len);
	crypto_xor(info, ctx->salt, ivsize);
}

static int seqiv_givencrypt(struct skcipher_givcrypt_request *req)
{
	struct crypto_ablkcipher *geniv = skcipher_givcrypt_reqtfm(req);
	struct seqiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);
	struct ablkcipher_request *subreq = skcipher_givcrypt_reqctx(req);
	crypto_completion_t compl;
	void *data;
	u8 *info;
	unsigned int ivsize;
	int err;

	ablkcipher_request_set_tfm(subreq, skcipher_geniv_cipher(geniv));

	compl = req->creq.base.complete;
	data = req->creq.base.data;
	info = req->creq.info;

	ivsize = crypto_ablkcipher_ivsize(geniv);

	if (unlikely(!IS_ALIGNED((unsigned long)info,
				 crypto_ablkcipher_alignmask(geniv) + 1))) {
		info = kmalloc(ivsize, req->creq.base.flags &
				       CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL:
								  GFP_ATOMIC);
		if (!info)
			return -ENOMEM;

		compl = seqiv_complete;
		data = req;
	}

	ablkcipher_request_set_callback(subreq, req->creq.base.flags, compl,
					data);
	ablkcipher_request_set_crypt(subreq, req->creq.src, req->creq.dst,
				     req->creq.nbytes, info);

	seqiv_geniv(ctx, info, req->seq, ivsize);
	memcpy(req->giv, info, ivsize);

	err = crypto_ablkcipher_encrypt(subreq);
	if (unlikely(info != req->creq.info))
		seqiv_complete2(req, err);
	return err;
}

static int seqiv_aead_givencrypt(struct aead_givcrypt_request *req)
{
	struct crypto_aead *geniv = aead_givcrypt_reqtfm(req);
	struct seqiv_ctx *ctx = crypto_aead_ctx(geniv);
	struct aead_request *areq = &req->areq;
	struct aead_request *subreq = aead_givcrypt_reqctx(req);
	crypto_completion_t compl;
	void *data;
	u8 *info;
	unsigned int ivsize;
	int err;

	aead_request_set_tfm(subreq, aead_geniv_base(geniv));

	compl = areq->base.complete;
	data = areq->base.data;
	info = areq->iv;

	ivsize = crypto_aead_ivsize(geniv);

	if (unlikely(!IS_ALIGNED((unsigned long)info,
				 crypto_aead_alignmask(geniv) + 1))) {
		info = kmalloc(ivsize, areq->base.flags &
				       CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL:
								  GFP_ATOMIC);
		if (!info)
			return -ENOMEM;

		compl = seqiv_aead_complete;
		data = req;
	}

	aead_request_set_callback(subreq, areq->base.flags, compl, data);
	aead_request_set_crypt(subreq, areq->src, areq->dst, areq->cryptlen,
			       info);
	aead_request_set_assoc(subreq, areq->assoc, areq->assoclen);

	seqiv_geniv(ctx, info, req->seq, ivsize);
	memcpy(req->giv, info, ivsize);

	err = crypto_aead_encrypt(subreq);
	if (unlikely(info != areq->iv))
		seqiv_aead_complete2(req, err);
	return err;
}

static int seqniv_aead_encrypt(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(geniv);
	struct seqniv_request_ctx *rctx = aead_request_ctx(req);
	struct aead_request *subreq = &rctx->subreq;
	struct scatterlist *dst;
	crypto_completion_t compl;
	void *data;
	unsigned int ivsize = 8;
	u8 buf[20] __attribute__ ((aligned(__alignof__(u32))));
	int err;

	if (req->cryptlen < ivsize)
		return -EINVAL;

	/* ESP AD is at most 12 bytes (ESN). */
	if (req->assoclen > 12)
		return -EINVAL;

	aead_request_set_tfm(subreq, ctx->geniv.child);

	compl = seqniv_aead_encrypt_complete;
	data = req;

	if (req->src != req->dst) {
		struct blkcipher_desc desc = {
			.tfm = ctx->null,
		};

		err = crypto_blkcipher_encrypt(&desc, req->dst, req->src,
					       req->assoclen + req->cryptlen);
		if (err)
			return err;
	}

	dst = scatterwalk_ffwd(rctx->dst, req->dst, ivsize);

	aead_request_set_callback(subreq, req->base.flags, compl, data);
	aead_request_set_crypt(subreq, dst, dst,
			       req->cryptlen - ivsize, req->iv);
	aead_request_set_ad(subreq, req->assoclen);

	memcpy(buf, req->iv, ivsize);
	crypto_xor(buf, ctx->salt, ivsize);
	memcpy(req->iv, buf, ivsize);

	/* Swap order of IV and ESP AD for ICV generation. */
	scatterwalk_map_and_copy(buf + ivsize, req->dst, 0, req->assoclen, 0);
	scatterwalk_map_and_copy(buf, req->dst, 0, req->assoclen + ivsize, 1);

	err = crypto_aead_encrypt(subreq);
	seqniv_aead_encrypt_complete2(req, err);
	return err;
}

static int seqiv_aead_encrypt(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(geniv);
	struct aead_request *subreq = aead_request_ctx(req);
	crypto_completion_t compl;
	void *data;
	u8 *info;
	unsigned int ivsize = 8;
	int err;

	if (req->cryptlen < ivsize)
		return -EINVAL;

	aead_request_set_tfm(subreq, ctx->geniv.child);

	compl = req->base.complete;
	data = req->base.data;
	info = req->iv;

	if (req->src != req->dst) {
		struct blkcipher_desc desc = {
			.tfm = ctx->null,
		};

		err = crypto_blkcipher_encrypt(&desc, req->dst, req->src,
					       req->assoclen + req->cryptlen);
		if (err)
			return err;
	}

	if (unlikely(!IS_ALIGNED((unsigned long)info,
				 crypto_aead_alignmask(geniv) + 1))) {
		info = kmalloc(ivsize, req->base.flags &
				       CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL:
								  GFP_ATOMIC);
		if (!info)
			return -ENOMEM;

		memcpy(info, req->iv, ivsize);
		compl = seqiv_aead_encrypt_complete;
		data = req;
	}

	aead_request_set_callback(subreq, req->base.flags, compl, data);
	aead_request_set_crypt(subreq, req->dst, req->dst,
			       req->cryptlen - ivsize, info);
	aead_request_set_ad(subreq, req->assoclen + ivsize);

	crypto_xor(info, ctx->salt, ivsize);
	scatterwalk_map_and_copy(info, req->dst, req->assoclen, ivsize, 1);

	err = crypto_aead_encrypt(subreq);
	if (unlikely(info != req->iv))
		seqiv_aead_encrypt_complete2(req, err);
	return err;
}

static int seqniv_aead_decrypt(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(geniv);
	struct seqniv_request_ctx *rctx = aead_request_ctx(req);
	struct aead_request *subreq = &rctx->subreq;
	struct scatterlist *dst;
	crypto_completion_t compl;
	void *data;
	unsigned int ivsize = 8;
	u8 buf[20];
	int err;

	if (req->cryptlen < ivsize + crypto_aead_authsize(geniv))
		return -EINVAL;

	aead_request_set_tfm(subreq, ctx->geniv.child);

	compl = req->base.complete;
	data = req->base.data;

	if (req->assoclen > 12)
		return -EINVAL;
	else if (req->assoclen > 8) {
		compl = seqniv_aead_decrypt_complete;
		data = req;
	}

	if (req->src != req->dst) {
		struct blkcipher_desc desc = {
			.tfm = ctx->null,
		};

		err = crypto_blkcipher_encrypt(&desc, req->dst, req->src,
					       req->assoclen + req->cryptlen);
		if (err)
			return err;
	}

	/* Move ESP AD forward for ICV generation. */
	scatterwalk_map_and_copy(buf, req->dst, 0, req->assoclen + ivsize, 0);
	memcpy(req->iv, buf + req->assoclen, ivsize);
	scatterwalk_map_and_copy(buf, req->dst, ivsize, req->assoclen, 1);

	dst = scatterwalk_ffwd(rctx->dst, req->dst, ivsize);

	aead_request_set_callback(subreq, req->base.flags, compl, data);
	aead_request_set_crypt(subreq, dst, dst,
			       req->cryptlen - ivsize, req->iv);
	aead_request_set_ad(subreq, req->assoclen);

	err = crypto_aead_decrypt(subreq);
	if (req->assoclen > 8)
		seqniv_aead_decrypt_complete2(req, err);
	return err;
}

static int seqiv_aead_decrypt(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(geniv);
	struct aead_request *subreq = aead_request_ctx(req);
	crypto_completion_t compl;
	void *data;
	unsigned int ivsize = 8;

	if (req->cryptlen < ivsize + crypto_aead_authsize(geniv))
		return -EINVAL;

	aead_request_set_tfm(subreq, ctx->geniv.child);

	compl = req->base.complete;
	data = req->base.data;

	aead_request_set_callback(subreq, req->base.flags, compl, data);
	aead_request_set_crypt(subreq, req->src, req->dst,
			       req->cryptlen - ivsize, req->iv);
	aead_request_set_ad(subreq, req->assoclen + ivsize);

	scatterwalk_map_and_copy(req->iv, req->src, req->assoclen, ivsize, 0);

	return crypto_aead_decrypt(subreq);
}

static int seqiv_init(struct crypto_tfm *tfm)
{
	struct crypto_ablkcipher *geniv = __crypto_ablkcipher_cast(tfm);
	struct seqiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);
	int err;

	spin_lock_init(&ctx->lock);

	tfm->crt_ablkcipher.reqsize = sizeof(struct ablkcipher_request);

	err = 0;
	if (!crypto_get_default_rng()) {
		crypto_ablkcipher_crt(geniv)->givencrypt = seqiv_givencrypt;
		err = crypto_rng_get_bytes(crypto_default_rng, ctx->salt,
					   crypto_ablkcipher_ivsize(geniv));
		crypto_put_default_rng();
	}

	return err ?: skcipher_geniv_init(tfm);
}

static int seqiv_old_aead_init(struct crypto_tfm *tfm)
{
	struct crypto_aead *geniv = __crypto_aead_cast(tfm);
	struct seqiv_ctx *ctx = crypto_aead_ctx(geniv);
	int err;

	spin_lock_init(&ctx->lock);

	crypto_aead_set_reqsize(__crypto_aead_cast(tfm),
				sizeof(struct aead_request));
	err = 0;
	if (!crypto_get_default_rng()) {
		geniv->givencrypt = seqiv_aead_givencrypt;
		err = crypto_rng_get_bytes(crypto_default_rng, ctx->salt,
					   crypto_aead_ivsize(geniv));
		crypto_put_default_rng();
	}

	return err ?: aead_geniv_init(tfm);
}

static int seqiv_aead_init_common(struct crypto_aead *geniv,
				  unsigned int reqsize)
{
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(geniv);
	int err;

	spin_lock_init(&ctx->geniv.lock);

	crypto_aead_set_reqsize(geniv, sizeof(struct aead_request));

	err = crypto_get_default_rng();
	if (err)
		goto out;

	err = crypto_rng_get_bytes(crypto_default_rng, ctx->salt,
				   crypto_aead_ivsize(geniv));
	crypto_put_default_rng();
	if (err)
		goto out;

	ctx->null = crypto_get_default_null_skcipher();
	err = PTR_ERR(ctx->null);
	if (IS_ERR(ctx->null))
		goto out;

	err = aead_geniv_init(crypto_aead_tfm(geniv));
	if (err)
		goto drop_null;

	ctx->geniv.child = geniv->child;
	geniv->child = geniv;

out:
	return err;

drop_null:
	crypto_put_default_null_skcipher();
	goto out;
}

static int seqiv_aead_init(struct crypto_aead *tfm)
{
	return seqiv_aead_init_common(tfm, sizeof(struct aead_request));
}

static int seqniv_aead_init(struct crypto_aead *tfm)
{
	return seqiv_aead_init_common(tfm, sizeof(struct seqniv_request_ctx));
}

static void seqiv_aead_exit(struct crypto_aead *tfm)
{
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_aead(ctx->geniv.child);
	crypto_put_default_null_skcipher();
}

static int seqiv_ablkcipher_create(struct crypto_template *tmpl,
				   struct rtattr **tb)
{
	struct crypto_instance *inst;
	int err;

	inst = skcipher_geniv_alloc(tmpl, tb, 0, 0);

	if (IS_ERR(inst))
		return PTR_ERR(inst);

	err = -EINVAL;
	if (inst->alg.cra_ablkcipher.ivsize < sizeof(u64))
		goto free_inst;

	inst->alg.cra_init = seqiv_init;
	inst->alg.cra_exit = skcipher_geniv_exit;

	inst->alg.cra_ctxsize += inst->alg.cra_ablkcipher.ivsize;
	inst->alg.cra_ctxsize += sizeof(struct seqiv_ctx);

	inst->alg.cra_alignmask |= __alignof__(u32) - 1;

	err = crypto_register_instance(tmpl, inst);
	if (err)
		goto free_inst;

out:
	return err;

free_inst:
	skcipher_geniv_free(inst);
	goto out;
}

static int seqiv_old_aead_create(struct crypto_template *tmpl,
				 struct aead_instance *aead)
{
	struct crypto_instance *inst = aead_crypto_instance(aead);
	int err = -EINVAL;

	if (inst->alg.cra_aead.ivsize < sizeof(u64))
		goto free_inst;

	inst->alg.cra_init = seqiv_old_aead_init;
	inst->alg.cra_exit = aead_geniv_exit;

	inst->alg.cra_ctxsize = inst->alg.cra_aead.ivsize;
	inst->alg.cra_ctxsize += sizeof(struct seqiv_ctx);

	err = crypto_register_instance(tmpl, inst);
	if (err)
		goto free_inst;

out:
	return err;

free_inst:
	aead_geniv_free(aead);
	goto out;
}

static int seqiv_aead_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct aead_instance *inst;
	struct crypto_aead_spawn *spawn;
	struct aead_alg *alg;
	int err;

	inst = aead_geniv_alloc(tmpl, tb, 0, 0);

	if (IS_ERR(inst))
		return PTR_ERR(inst);

	inst->alg.base.cra_alignmask |= __alignof__(u32) - 1;

	if (inst->alg.base.cra_aead.encrypt)
		return seqiv_old_aead_create(tmpl, inst);

	spawn = aead_instance_ctx(inst);
	alg = crypto_spawn_aead_alg(spawn);

	if (alg->base.cra_aead.encrypt)
		goto done;

	err = -EINVAL;
	if (inst->alg.ivsize != sizeof(u64))
		goto free_inst;

	inst->alg.encrypt = seqiv_aead_encrypt;
	inst->alg.decrypt = seqiv_aead_decrypt;

	inst->alg.init = seqiv_aead_init;
	inst->alg.exit = seqiv_aead_exit;

	inst->alg.base.cra_ctxsize = sizeof(struct seqiv_aead_ctx);
	inst->alg.base.cra_ctxsize += inst->alg.ivsize;

done:
	err = aead_register_instance(tmpl, inst);
	if (err)
		goto free_inst;

out:
	return err;

free_inst:
	aead_geniv_free(inst);
	goto out;
}

static int seqiv_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct crypto_attr_type *algt;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & CRYPTO_ALG_TYPE_MASK)
		err = seqiv_ablkcipher_create(tmpl, tb);
	else
		err = seqiv_aead_create(tmpl, tb);

	return err;
}

static int seqniv_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct aead_instance *inst;
	struct crypto_aead_spawn *spawn;
	struct aead_alg *alg;
	int err;

	inst = aead_geniv_alloc(tmpl, tb, 0, 0);
	err = PTR_ERR(inst);
	if (IS_ERR(inst))
		goto out;

	spawn = aead_instance_ctx(inst);
	alg = crypto_spawn_aead_alg(spawn);

	if (alg->base.cra_aead.encrypt)
		goto done;

	err = -EINVAL;
	if (inst->alg.ivsize != sizeof(u64))
		goto free_inst;

	inst->alg.encrypt = seqniv_aead_encrypt;
	inst->alg.decrypt = seqniv_aead_decrypt;

	inst->alg.init = seqniv_aead_init;
	inst->alg.exit = seqiv_aead_exit;

	if ((alg->base.cra_flags & CRYPTO_ALG_AEAD_NEW)) {
		inst->alg.encrypt = seqiv_aead_encrypt;
		inst->alg.decrypt = seqiv_aead_decrypt;

		inst->alg.init = seqiv_aead_init;
	}

	inst->alg.base.cra_alignmask |= __alignof__(u32) - 1;
	inst->alg.base.cra_ctxsize = sizeof(struct seqiv_aead_ctx);
	inst->alg.base.cra_ctxsize += inst->alg.ivsize;

done:
	err = aead_register_instance(tmpl, inst);
	if (err)
		goto free_inst;

out:
	return err;

free_inst:
	aead_geniv_free(inst);
	goto out;
}

static void seqiv_free(struct crypto_instance *inst)
{
	if ((inst->alg.cra_flags ^ CRYPTO_ALG_TYPE_AEAD) & CRYPTO_ALG_TYPE_MASK)
		skcipher_geniv_free(inst);
	else
		aead_geniv_free(aead_instance(inst));
}

static struct crypto_template seqiv_tmpl = {
	.name = "seqiv",
	.create = seqiv_create,
	.free = seqiv_free,
	.module = THIS_MODULE,
};

static struct crypto_template seqniv_tmpl = {
	.name = "seqniv",
	.create = seqniv_create,
	.free = seqiv_free,
	.module = THIS_MODULE,
};

static int __init seqiv_module_init(void)
{
	int err;

	err = crypto_register_template(&seqiv_tmpl);
	if (err)
		goto out;

	err = crypto_register_template(&seqniv_tmpl);
	if (err)
		goto out_undo_niv;

out:
	return err;

out_undo_niv:
	crypto_unregister_template(&seqiv_tmpl);
	goto out;
}

static void __exit seqiv_module_exit(void)
{
	crypto_unregister_template(&seqniv_tmpl);
	crypto_unregister_template(&seqiv_tmpl);
}

module_init(seqiv_module_init);
module_exit(seqiv_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Sequence Number IV Generator");
MODULE_ALIAS_CRYPTO("seqiv");
MODULE_ALIAS_CRYPTO("seqniv");
