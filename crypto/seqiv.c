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

#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/string.h>

struct seqiv_ctx {
	spinlock_t lock;
	u8 salt[] __attribute__ ((aligned(__alignof__(u32))));
};

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

static int seqiv_givencrypt(struct skcipher_givcrypt_request *req)
{
	struct crypto_ablkcipher *geniv = skcipher_givcrypt_reqtfm(req);
	struct seqiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);
	struct ablkcipher_request *subreq = skcipher_givcrypt_reqctx(req);
	crypto_completion_t complete;
	void *data;
	u8 *info;
	__be64 seq;
	unsigned int ivsize;
	unsigned int len;
	int err;

	ablkcipher_request_set_tfm(subreq, skcipher_geniv_cipher(geniv));

	complete = req->creq.base.complete;
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

		complete = seqiv_complete;
		data = req;
	}

	ablkcipher_request_set_callback(subreq, req->creq.base.flags, complete,
					data);
	ablkcipher_request_set_crypt(subreq, req->creq.src, req->creq.dst,
				     req->creq.nbytes, info);

	len = ivsize;
	if (ivsize > sizeof(u64)) {
		memset(info, 0, ivsize - sizeof(u64));
		len = sizeof(u64);
	}
	seq = cpu_to_be64(req->seq);
	memcpy(info + ivsize - len, &seq, len);
	crypto_xor(info, ctx->salt, ivsize);

	memcpy(req->giv, info, ivsize);

	err = crypto_ablkcipher_encrypt(subreq);
	if (unlikely(info != req->creq.info))
		seqiv_complete2(req, err);
	return err;
}

static int seqiv_givencrypt_first(struct skcipher_givcrypt_request *req)
{
	struct crypto_ablkcipher *geniv = skcipher_givcrypt_reqtfm(req);
	struct seqiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);

	spin_lock_bh(&ctx->lock);
	if (crypto_ablkcipher_crt(geniv)->givencrypt != seqiv_givencrypt_first)
		goto unlock;

	crypto_ablkcipher_crt(geniv)->givencrypt = seqiv_givencrypt;
	get_random_bytes(ctx->salt, crypto_ablkcipher_ivsize(geniv));

unlock:
	spin_unlock_bh(&ctx->lock);

	return seqiv_givencrypt(req);
}

static int seqiv_init(struct crypto_tfm *tfm)
{
	struct crypto_ablkcipher *geniv = __crypto_ablkcipher_cast(tfm);
	struct seqiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);

	spin_lock_init(&ctx->lock);

	tfm->crt_ablkcipher.reqsize = sizeof(struct ablkcipher_request);

	return skcipher_geniv_init(tfm);
}

static struct crypto_template seqiv_tmpl;

static struct crypto_instance *seqiv_alloc(struct rtattr **tb)
{
	struct crypto_instance *inst;

	inst = skcipher_geniv_alloc(&seqiv_tmpl, tb, 0, 0);
	if (IS_ERR(inst))
		goto out;

	inst->alg.cra_ablkcipher.givencrypt = seqiv_givencrypt_first;

	inst->alg.cra_init = seqiv_init;
	inst->alg.cra_exit = skcipher_geniv_exit;

	inst->alg.cra_alignmask |= __alignof__(u32) - 1;

	inst->alg.cra_ctxsize = sizeof(struct seqiv_ctx);
	inst->alg.cra_ctxsize += inst->alg.cra_ablkcipher.ivsize;

out:
	return inst;
}

static struct crypto_template seqiv_tmpl = {
	.name = "seqiv",
	.alloc = seqiv_alloc,
	.free = skcipher_geniv_free,
	.module = THIS_MODULE,
};

static int __init seqiv_module_init(void)
{
	return crypto_register_template(&seqiv_tmpl);
}

static void __exit seqiv_module_exit(void)
{
	crypto_unregister_template(&seqiv_tmpl);
}

module_init(seqiv_module_init);
module_exit(seqiv_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Sequence Number IV Generator");
