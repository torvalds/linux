/*
 * Cryptographic API.
 *
 * HMAC: Keyed-Hashing for Message Authentication (RFC2104).
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * The HMAC implementation is derived from USAGI.
 * Copyright (c) 2002 Kazunori Miyazawa <miyazawa@linux-ipv6.org> / USAGI
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/hash.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/string.h>

struct hmac_ctx {
	struct shash_desc *desc;
};

static inline void *align_ptr(void *p, unsigned int align)
{
	return (void *)ALIGN((unsigned long)p, align);
}

static inline struct hmac_ctx *hmac_ctx(struct crypto_shash *tfm)
{
	return align_ptr(crypto_shash_ctx_aligned(tfm) +
			 crypto_shash_blocksize(tfm) * 2 +
			 crypto_shash_digestsize(tfm),
			 crypto_tfm_ctx_alignment());
}

static int hmac_setkey(struct crypto_shash *parent,
		       const u8 *inkey, unsigned int keylen)
{
	int bs = crypto_shash_blocksize(parent);
	int ds = crypto_shash_digestsize(parent);
	char *ipad = crypto_shash_ctx_aligned(parent);
	char *opad = ipad + bs;
	char *digest = opad + bs;
	struct hmac_ctx *ctx = align_ptr(digest + ds,
					 crypto_tfm_ctx_alignment());
	unsigned int i;

	if (keylen > bs) {
		int err;

		ctx->desc->flags = crypto_shash_get_flags(parent) &
				   CRYPTO_TFM_REQ_MAY_SLEEP;

		err = crypto_shash_digest(ctx->desc, inkey, keylen, digest);
		if (err)
			return err;

		inkey = digest;
		keylen = ds;
	}

	memcpy(ipad, inkey, keylen);
	memset(ipad + keylen, 0, bs - keylen);
	memcpy(opad, ipad, bs);

	for (i = 0; i < bs; i++) {
		ipad[i] ^= 0x36;
		opad[i] ^= 0x5c;
	}

	return 0;
}

static int hmac_init(struct shash_desc *pdesc)
{
	struct crypto_shash *parent = pdesc->tfm;
	int bs = crypto_shash_blocksize(parent);
	int ds = crypto_shash_digestsize(parent);
	char *ipad = crypto_shash_ctx_aligned(parent);
	struct hmac_ctx *ctx = align_ptr(ipad + bs * 2 + ds,
					 crypto_tfm_ctx_alignment());
	struct shash_desc *desc = shash_desc_ctx(pdesc);

	desc->tfm = ctx->desc->tfm;
	desc->flags = pdesc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_shash_init(desc) ?:
	       crypto_shash_update(desc, ipad, bs);
}

static int hmac_update(struct shash_desc *pdesc,
		       const u8 *data, unsigned int nbytes)
{
	struct shash_desc *desc = shash_desc_ctx(pdesc);

	desc->flags = pdesc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_shash_update(desc, data, nbytes);
}

static int hmac_final(struct shash_desc *pdesc, u8 *out)
{
	struct crypto_shash *parent = pdesc->tfm;
	int bs = crypto_shash_blocksize(parent);
	int ds = crypto_shash_digestsize(parent);
	char *opad = crypto_shash_ctx_aligned(parent) + bs;
	char *digest = opad + bs;
	struct shash_desc *desc = shash_desc_ctx(pdesc);

	desc->flags = pdesc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_shash_final(desc, digest) ?:
	       crypto_shash_digest(desc, opad, bs + ds, out);
}

static int hmac_finup(struct shash_desc *pdesc, const u8 *data,
		      unsigned int nbytes, u8 *out)
{

	struct crypto_shash *parent = pdesc->tfm;
	int bs = crypto_shash_blocksize(parent);
	int ds = crypto_shash_digestsize(parent);
	char *opad = crypto_shash_ctx_aligned(parent) + bs;
	char *digest = opad + bs;
	struct shash_desc *desc = shash_desc_ctx(pdesc);

	desc->flags = pdesc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_shash_finup(desc, data, nbytes, digest) ?:
	       crypto_shash_digest(desc, opad, bs + ds, out);
}

static int hmac_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_shash *parent = __crypto_shash_cast(tfm);
	struct crypto_shash *hash;
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_shash_spawn *spawn = crypto_instance_ctx(inst);
	struct hmac_ctx *ctx = hmac_ctx(parent);

	hash = crypto_spawn_shash(spawn);
	if (IS_ERR(hash))
		return PTR_ERR(hash);

	parent->descsize = sizeof(struct shash_desc) +
			   crypto_shash_descsize(hash);

	ctx->desc = kmalloc(parent->descsize, GFP_KERNEL);
	if (!ctx->desc) {
		crypto_free_shash(hash);
		return -ENOMEM;
	}

	ctx->desc->tfm = hash;
	return 0;
}

static void hmac_exit_tfm(struct crypto_tfm *tfm)
{
	struct hmac_ctx *ctx = hmac_ctx(__crypto_shash_cast(tfm));
	crypto_free_shash(ctx->desc->tfm);
	kzfree(ctx->desc);
}

static int hmac_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct shash_instance *inst;
	struct crypto_alg *alg;
	struct shash_alg *salg;
	int err;
	int ds;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SHASH);
	if (err)
		return err;

	salg = shash_attr_alg(tb[1], 0, 0);
	if (IS_ERR(salg))
		return PTR_ERR(salg);

	err = -EINVAL;
	ds = salg->digestsize;
	alg = &salg->base;
	if (ds > alg->cra_blocksize)
		goto out_put_alg;

	inst = shash_alloc_instance("hmac", alg);
	err = PTR_ERR(inst);
	if (IS_ERR(inst))
		goto out_put_alg;

	err = crypto_init_shash_spawn(shash_instance_ctx(inst), salg,
				      shash_crypto_instance(inst));
	if (err)
		goto out_free_inst;

	inst->alg.base.cra_priority = alg->cra_priority;
	inst->alg.base.cra_blocksize = alg->cra_blocksize;
	inst->alg.base.cra_alignmask = alg->cra_alignmask;

	inst->alg.digestsize = ds;

	inst->alg.base.cra_ctxsize = sizeof(struct hmac_ctx) +
				     ALIGN(alg->cra_blocksize * 2 + ds,
					   crypto_tfm_ctx_alignment());

	inst->alg.base.cra_init = hmac_init_tfm;
	inst->alg.base.cra_exit = hmac_exit_tfm;

	inst->alg.init = hmac_init;
	inst->alg.update = hmac_update;
	inst->alg.final = hmac_final;
	inst->alg.finup = hmac_finup;
	inst->alg.setkey = hmac_setkey;

	err = shash_register_instance(tmpl, inst);
	if (err) {
out_free_inst:
		shash_free_instance(shash_crypto_instance(inst));
	}

out_put_alg:
	crypto_mod_put(alg);
	return err;
}

static struct crypto_template hmac_tmpl = {
	.name = "hmac",
	.create = hmac_create,
	.free = shash_free_instance,
	.module = THIS_MODULE,
};

static int __init hmac_module_init(void)
{
	return crypto_register_template(&hmac_tmpl);
}

static void __exit hmac_module_exit(void)
{
	crypto_unregister_template(&hmac_tmpl);
}

module_init(hmac_module_init);
module_exit(hmac_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HMAC hash algorithm");
