// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * echainiv: Encrypted Chain IV Generator
 *
 * This generator generates an IV based on a sequence number by multiplying
 * it with a salt and then encrypting it with the same key as used to encrypt
 * the plain text.  This algorithm requires that the block size be equal
 * to the IV size.  It is mainly useful for CBC.
 *
 * This generator can only be used by algorithms where authentication
 * is performed after encryption (i.e., authenc).
 *
 * Copyright (c) 2015 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/internal/geniv.h>
#include <crypto/scatterwalk.h>
#include <crypto/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

static int echainiv_encrypt(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct aead_geniv_ctx *ctx = crypto_aead_ctx(geniv);
	struct aead_request *subreq = aead_request_ctx(req);
	__be64 nseqno;
	u64 seqno;
	u8 *info;
	unsigned int ivsize = crypto_aead_ivsize(geniv);
	int err;

	if (req->cryptlen < ivsize)
		return -EINVAL;

	aead_request_set_tfm(subreq, ctx->child);

	info = req->iv;

	if (req->src != req->dst) {
		SYNC_SKCIPHER_REQUEST_ON_STACK(nreq, ctx->sknull);

		skcipher_request_set_sync_tfm(nreq, ctx->sknull);
		skcipher_request_set_callback(nreq, req->base.flags,
					      NULL, NULL);
		skcipher_request_set_crypt(nreq, req->src, req->dst,
					   req->assoclen + req->cryptlen,
					   NULL);

		err = crypto_skcipher_encrypt(nreq);
		if (err)
			return err;
	}

	aead_request_set_callback(subreq, req->base.flags,
				  req->base.complete, req->base.data);
	aead_request_set_crypt(subreq, req->dst, req->dst,
			       req->cryptlen, info);
	aead_request_set_ad(subreq, req->assoclen);

	memcpy(&nseqno, info + ivsize - 8, 8);
	seqno = be64_to_cpu(nseqno);
	memset(info, 0, ivsize);

	scatterwalk_map_and_copy(info, req->dst, req->assoclen, ivsize, 1);

	do {
		u64 a;

		memcpy(&a, ctx->salt + ivsize - 8, 8);

		a |= 1;
		a *= seqno;

		memcpy(info + ivsize - 8, &a, 8);
	} while ((ivsize -= 8));

	return crypto_aead_encrypt(subreq);
}

static int echainiv_decrypt(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct aead_geniv_ctx *ctx = crypto_aead_ctx(geniv);
	struct aead_request *subreq = aead_request_ctx(req);
	crypto_completion_t compl;
	void *data;
	unsigned int ivsize = crypto_aead_ivsize(geniv);

	if (req->cryptlen < ivsize)
		return -EINVAL;

	aead_request_set_tfm(subreq, ctx->child);

	compl = req->base.complete;
	data = req->base.data;

	aead_request_set_callback(subreq, req->base.flags, compl, data);
	aead_request_set_crypt(subreq, req->src, req->dst,
			       req->cryptlen - ivsize, req->iv);
	aead_request_set_ad(subreq, req->assoclen + ivsize);

	scatterwalk_map_and_copy(req->iv, req->src, req->assoclen, ivsize, 0);

	return crypto_aead_decrypt(subreq);
}

static int echainiv_aead_create(struct crypto_template *tmpl,
				struct rtattr **tb)
{
	struct aead_instance *inst;
	int err;

	inst = aead_geniv_alloc(tmpl, tb, 0, 0);

	if (IS_ERR(inst))
		return PTR_ERR(inst);

	err = -EINVAL;
	if (inst->alg.ivsize & (sizeof(u64) - 1) || !inst->alg.ivsize)
		goto free_inst;

	inst->alg.encrypt = echainiv_encrypt;
	inst->alg.decrypt = echainiv_decrypt;

	inst->alg.init = aead_init_geniv;
	inst->alg.exit = aead_exit_geniv;

	inst->alg.base.cra_ctxsize = sizeof(struct aead_geniv_ctx);
	inst->alg.base.cra_ctxsize += inst->alg.ivsize;

	err = aead_register_instance(tmpl, inst);
	if (err) {
free_inst:
		inst->free(inst);
	}
	return err;
}

static struct crypto_template echainiv_tmpl = {
	.name = "echainiv",
	.create = echainiv_aead_create,
	.module = THIS_MODULE,
};

static int __init echainiv_module_init(void)
{
	return crypto_register_template(&echainiv_tmpl);
}

static void __exit echainiv_module_exit(void)
{
	crypto_unregister_template(&echainiv_tmpl);
}

subsys_initcall(echainiv_module_init);
module_exit(echainiv_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Encrypted Chain IV Generator");
MODULE_ALIAS_CRYPTO("echainiv");
