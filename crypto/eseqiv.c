/*
 * eseqiv: Encrypted Sequence Number IV Generator
 *
 * This generator generates an IV based on a sequence number by xoring it
 * with a salt and then encrypting it with the same key as used to encrypt
 * the plain text.  This algorithm requires that the block size be equal
 * to the IV size.  It is mainly useful for CBC.
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
#include <crypto/rng.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/string.h>

struct eseqiv_request_ctx {
	struct scatterlist src[2];
	struct scatterlist dst[2];
	char tail[];
};

struct eseqiv_ctx {
	spinlock_t lock;
	unsigned int reqoff;
	char salt[];
};

static void eseqiv_complete2(struct skcipher_givcrypt_request *req)
{
	struct crypto_ablkcipher *geniv = skcipher_givcrypt_reqtfm(req);
	struct eseqiv_request_ctx *reqctx = skcipher_givcrypt_reqctx(req);

	memcpy(req->giv, PTR_ALIGN((u8 *)reqctx->tail,
			 crypto_ablkcipher_alignmask(geniv) + 1),
	       crypto_ablkcipher_ivsize(geniv));
}

static void eseqiv_complete(struct crypto_async_request *base, int err)
{
	struct skcipher_givcrypt_request *req = base->data;

	if (err)
		goto out;

	eseqiv_complete2(req);

out:
	skcipher_givcrypt_complete(req, err);
}

static int eseqiv_givencrypt(struct skcipher_givcrypt_request *req)
{
	struct crypto_ablkcipher *geniv = skcipher_givcrypt_reqtfm(req);
	struct eseqiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);
	struct eseqiv_request_ctx *reqctx = skcipher_givcrypt_reqctx(req);
	struct ablkcipher_request *subreq;
	crypto_completion_t compl;
	void *data;
	struct scatterlist *osrc, *odst;
	struct scatterlist *dst;
	struct page *srcp;
	struct page *dstp;
	u8 *giv;
	u8 *vsrc;
	u8 *vdst;
	__be64 seq;
	unsigned int ivsize;
	unsigned int len;
	int err;

	subreq = (void *)(reqctx->tail + ctx->reqoff);
	ablkcipher_request_set_tfm(subreq, skcipher_geniv_cipher(geniv));

	giv = req->giv;
	compl = req->creq.base.complete;
	data = req->creq.base.data;

	osrc = req->creq.src;
	odst = req->creq.dst;
	srcp = sg_page(osrc);
	dstp = sg_page(odst);
	vsrc = PageHighMem(srcp) ? NULL : page_address(srcp) + osrc->offset;
	vdst = PageHighMem(dstp) ? NULL : page_address(dstp) + odst->offset;

	ivsize = crypto_ablkcipher_ivsize(geniv);

	if (vsrc != giv + ivsize && vdst != giv + ivsize) {
		giv = PTR_ALIGN((u8 *)reqctx->tail,
				crypto_ablkcipher_alignmask(geniv) + 1);
		compl = eseqiv_complete;
		data = req;
	}

	ablkcipher_request_set_callback(subreq, req->creq.base.flags, compl,
					data);

	sg_init_table(reqctx->src, 2);
	sg_set_buf(reqctx->src, giv, ivsize);
	scatterwalk_crypto_chain(reqctx->src, osrc, vsrc == giv + ivsize, 2);

	dst = reqctx->src;
	if (osrc != odst) {
		sg_init_table(reqctx->dst, 2);
		sg_set_buf(reqctx->dst, giv, ivsize);
		scatterwalk_crypto_chain(reqctx->dst, odst, vdst == giv + ivsize, 2);

		dst = reqctx->dst;
	}

	ablkcipher_request_set_crypt(subreq, reqctx->src, dst,
				     req->creq.nbytes + ivsize,
				     req->creq.info);

	memcpy(req->creq.info, ctx->salt, ivsize);

	len = ivsize;
	if (ivsize > sizeof(u64)) {
		memset(req->giv, 0, ivsize - sizeof(u64));
		len = sizeof(u64);
	}
	seq = cpu_to_be64(req->seq);
	memcpy(req->giv + ivsize - len, &seq, len);

	err = crypto_ablkcipher_encrypt(subreq);
	if (err)
		goto out;

	if (giv != req->giv)
		eseqiv_complete2(req);

out:
	return err;
}

static int eseqiv_givencrypt_first(struct skcipher_givcrypt_request *req)
{
	struct crypto_ablkcipher *geniv = skcipher_givcrypt_reqtfm(req);
	struct eseqiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);
	int err = 0;

	spin_lock_bh(&ctx->lock);
	if (crypto_ablkcipher_crt(geniv)->givencrypt != eseqiv_givencrypt_first)
		goto unlock;

	crypto_ablkcipher_crt(geniv)->givencrypt = eseqiv_givencrypt;
	err = crypto_rng_get_bytes(crypto_default_rng, ctx->salt,
				   crypto_ablkcipher_ivsize(geniv));

unlock:
	spin_unlock_bh(&ctx->lock);

	if (err)
		return err;

	return eseqiv_givencrypt(req);
}

static int eseqiv_init(struct crypto_tfm *tfm)
{
	struct crypto_ablkcipher *geniv = __crypto_ablkcipher_cast(tfm);
	struct eseqiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);
	unsigned long alignmask;
	unsigned int reqsize;

	spin_lock_init(&ctx->lock);

	alignmask = crypto_tfm_ctx_alignment() - 1;
	reqsize = sizeof(struct eseqiv_request_ctx);

	if (alignmask & reqsize) {
		alignmask &= reqsize;
		alignmask--;
	}

	alignmask = ~alignmask;
	alignmask &= crypto_ablkcipher_alignmask(geniv);

	reqsize += alignmask;
	reqsize += crypto_ablkcipher_ivsize(geniv);
	reqsize = ALIGN(reqsize, crypto_tfm_ctx_alignment());

	ctx->reqoff = reqsize - sizeof(struct eseqiv_request_ctx);

	tfm->crt_ablkcipher.reqsize = reqsize +
				      sizeof(struct ablkcipher_request);

	return skcipher_geniv_init(tfm);
}

static struct crypto_template eseqiv_tmpl;

static struct crypto_instance *eseqiv_alloc(struct rtattr **tb)
{
	struct crypto_instance *inst;
	int err;

	err = crypto_get_default_rng();
	if (err)
		return ERR_PTR(err);

	inst = skcipher_geniv_alloc(&eseqiv_tmpl, tb, 0, 0);
	if (IS_ERR(inst))
		goto put_rng;

	err = -EINVAL;
	if (inst->alg.cra_ablkcipher.ivsize != inst->alg.cra_blocksize)
		goto free_inst;

	inst->alg.cra_ablkcipher.givencrypt = eseqiv_givencrypt_first;

	inst->alg.cra_init = eseqiv_init;
	inst->alg.cra_exit = skcipher_geniv_exit;

	inst->alg.cra_ctxsize = sizeof(struct eseqiv_ctx);
	inst->alg.cra_ctxsize += inst->alg.cra_ablkcipher.ivsize;

out:
	return inst;

free_inst:
	skcipher_geniv_free(inst);
	inst = ERR_PTR(err);
put_rng:
	crypto_put_default_rng();
	goto out;
}

static void eseqiv_free(struct crypto_instance *inst)
{
	skcipher_geniv_free(inst);
	crypto_put_default_rng();
}

static struct crypto_template eseqiv_tmpl = {
	.name = "eseqiv",
	.alloc = eseqiv_alloc,
	.free = eseqiv_free,
	.module = THIS_MODULE,
};

static int __init eseqiv_module_init(void)
{
	return crypto_register_template(&eseqiv_tmpl);
}

static void __exit eseqiv_module_exit(void)
{
	crypto_unregister_template(&eseqiv_tmpl);
}

module_init(eseqiv_module_init);
module_exit(eseqiv_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Encrypted Sequence Number IV Generator");
