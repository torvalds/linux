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

#include <crypto/algapi.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/string.h>

struct hmac_ctx {
	struct crypto_hash *child;
};

static inline void *align_ptr(void *p, unsigned int align)
{
	return (void *)ALIGN((unsigned long)p, align);
}

static inline struct hmac_ctx *hmac_ctx(struct crypto_hash *tfm)
{
	return align_ptr(crypto_hash_ctx_aligned(tfm) +
			 crypto_hash_blocksize(tfm) * 2 +
			 crypto_hash_digestsize(tfm), sizeof(void *));
}

static int hmac_setkey(struct crypto_hash *parent,
		       const u8 *inkey, unsigned int keylen)
{
	int bs = crypto_hash_blocksize(parent);
	int ds = crypto_hash_digestsize(parent);
	char *ipad = crypto_hash_ctx_aligned(parent);
	char *opad = ipad + bs;
	char *digest = opad + bs;
	struct hmac_ctx *ctx = align_ptr(digest + ds, sizeof(void *));
	struct crypto_hash *tfm = ctx->child;
	unsigned int i;

	if (keylen > bs) {
		struct hash_desc desc;
		struct scatterlist tmp;
		int err;

		desc.tfm = tfm;
		desc.flags = crypto_hash_get_flags(parent);
		desc.flags &= CRYPTO_TFM_REQ_MAY_SLEEP;
		sg_init_one(&tmp, inkey, keylen);

		err = crypto_hash_digest(&desc, &tmp, keylen, digest);
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

static int hmac_init(struct hash_desc *pdesc)
{
	struct crypto_hash *parent = pdesc->tfm;
	int bs = crypto_hash_blocksize(parent);
	int ds = crypto_hash_digestsize(parent);
	char *ipad = crypto_hash_ctx_aligned(parent);
	struct hmac_ctx *ctx = align_ptr(ipad + bs * 2 + ds, sizeof(void *));
	struct hash_desc desc;
	struct scatterlist tmp;
	int err;

	desc.tfm = ctx->child;
	desc.flags = pdesc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	sg_init_one(&tmp, ipad, bs);

	err = crypto_hash_init(&desc);
	if (unlikely(err))
		return err;

	return crypto_hash_update(&desc, &tmp, bs);
}

static int hmac_update(struct hash_desc *pdesc,
		       struct scatterlist *sg, unsigned int nbytes)
{
	struct hmac_ctx *ctx = hmac_ctx(pdesc->tfm);
	struct hash_desc desc;

	desc.tfm = ctx->child;
	desc.flags = pdesc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_hash_update(&desc, sg, nbytes);
}

static int hmac_final(struct hash_desc *pdesc, u8 *out)
{
	struct crypto_hash *parent = pdesc->tfm;
	int bs = crypto_hash_blocksize(parent);
	int ds = crypto_hash_digestsize(parent);
	char *opad = crypto_hash_ctx_aligned(parent) + bs;
	char *digest = opad + bs;
	struct hmac_ctx *ctx = align_ptr(digest + ds, sizeof(void *));
	struct hash_desc desc;
	struct scatterlist tmp;
	int err;

	desc.tfm = ctx->child;
	desc.flags = pdesc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	sg_init_one(&tmp, opad, bs + ds);

	err = crypto_hash_final(&desc, digest);
	if (unlikely(err))
		return err;

	return crypto_hash_digest(&desc, &tmp, bs + ds, out);
}

static int hmac_digest(struct hash_desc *pdesc, struct scatterlist *sg,
		       unsigned int nbytes, u8 *out)
{
	struct crypto_hash *parent = pdesc->tfm;
	int bs = crypto_hash_blocksize(parent);
	int ds = crypto_hash_digestsize(parent);
	char *ipad = crypto_hash_ctx_aligned(parent);
	char *opad = ipad + bs;
	char *digest = opad + bs;
	struct hmac_ctx *ctx = align_ptr(digest + ds, sizeof(void *));
	struct hash_desc desc;
	struct scatterlist sg1[2];
	struct scatterlist sg2[1];
	int err;

	desc.tfm = ctx->child;
	desc.flags = pdesc->flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	sg_init_table(sg1, 2);
	sg_set_buf(sg1, ipad, bs);
	sg_set_page(&sg1[1], (void *) sg, 0, 0);

	sg_init_table(sg2, 1);
	sg_set_buf(sg2, opad, bs + ds);

	err = crypto_hash_digest(&desc, sg1, nbytes + bs, digest);
	if (unlikely(err))
		return err;

	return crypto_hash_digest(&desc, sg2, bs + ds, out);
}

static int hmac_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_hash *hash;
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_spawn *spawn = crypto_instance_ctx(inst);
	struct hmac_ctx *ctx = hmac_ctx(__crypto_hash_cast(tfm));

	hash = crypto_spawn_hash(spawn);
	if (IS_ERR(hash))
		return PTR_ERR(hash);

	ctx->child = hash;
	return 0;
}

static void hmac_exit_tfm(struct crypto_tfm *tfm)
{
	struct hmac_ctx *ctx = hmac_ctx(__crypto_hash_cast(tfm));
	crypto_free_hash(ctx->child);
}

static void hmac_free(struct crypto_instance *inst)
{
	crypto_drop_spawn(crypto_instance_ctx(inst));
	kfree(inst);
}

static struct crypto_instance *hmac_alloc(struct rtattr **tb)
{
	struct crypto_instance *inst;
	struct crypto_alg *alg;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_HASH);
	if (err)
		return ERR_PTR(err);

	alg = crypto_get_attr_alg(tb, CRYPTO_ALG_TYPE_HASH,
				  CRYPTO_ALG_TYPE_HASH_MASK);
	if (IS_ERR(alg))
		return ERR_PTR(PTR_ERR(alg));

	inst = crypto_alloc_instance("hmac", alg);
	if (IS_ERR(inst))
		goto out_put_alg;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_HASH;
	inst->alg.cra_priority = alg->cra_priority;
	inst->alg.cra_blocksize = alg->cra_blocksize;
	inst->alg.cra_alignmask = alg->cra_alignmask;
	inst->alg.cra_type = &crypto_hash_type;

	inst->alg.cra_hash.digestsize =
		(alg->cra_flags & CRYPTO_ALG_TYPE_MASK) ==
		CRYPTO_ALG_TYPE_HASH ? alg->cra_hash.digestsize :
				       alg->cra_digest.dia_digestsize;

	inst->alg.cra_ctxsize = sizeof(struct hmac_ctx) +
				ALIGN(inst->alg.cra_blocksize * 2 +
				      inst->alg.cra_hash.digestsize,
				      sizeof(void *));

	inst->alg.cra_init = hmac_init_tfm;
	inst->alg.cra_exit = hmac_exit_tfm;

	inst->alg.cra_hash.init = hmac_init;
	inst->alg.cra_hash.update = hmac_update;
	inst->alg.cra_hash.final = hmac_final;
	inst->alg.cra_hash.digest = hmac_digest;
	inst->alg.cra_hash.setkey = hmac_setkey;

out_put_alg:
	crypto_mod_put(alg);
	return inst;
}

static struct crypto_template hmac_tmpl = {
	.name = "hmac",
	.alloc = hmac_alloc,
	.free = hmac_free,
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
