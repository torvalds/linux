// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#include <crypto/hmac.h>
#include <crypto/internal/hash.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/string.h>

struct hmac_ctx {
	struct crypto_shash *hash;
};

static inline void *align_ptr(void *p, unsigned int align)
{
	return (void *)ALIGN((unsigned long)p, align);
}

static inline struct hmac_ctx *hmac_ctx(struct crypto_shash *tfm)
{
	return align_ptr(crypto_shash_ctx_aligned(tfm) +
			 crypto_shash_statesize(tfm) * 2,
			 crypto_tfm_ctx_alignment());
}

static int hmac_setkey(struct crypto_shash *parent,
		       const u8 *inkey, unsigned int keylen)
{
	int bs = crypto_shash_blocksize(parent);
	int ds = crypto_shash_digestsize(parent);
	int ss = crypto_shash_statesize(parent);
	char *ipad = crypto_shash_ctx_aligned(parent);
	char *opad = ipad + ss;
	struct hmac_ctx *ctx = align_ptr(opad + ss,
					 crypto_tfm_ctx_alignment());
	struct crypto_shash *hash = ctx->hash;
	SHASH_DESC_ON_STACK(shash, hash);
	unsigned int i;

	shash->tfm = hash;

	if (keylen > bs) {
		int err;

		err = crypto_shash_digest(shash, inkey, keylen, ipad);
		if (err)
			return err;

		keylen = ds;
	} else
		memcpy(ipad, inkey, keylen);

	memset(ipad + keylen, 0, bs - keylen);
	memcpy(opad, ipad, bs);

	for (i = 0; i < bs; i++) {
		ipad[i] ^= HMAC_IPAD_VALUE;
		opad[i] ^= HMAC_OPAD_VALUE;
	}

	return crypto_shash_init(shash) ?:
	       crypto_shash_update(shash, ipad, bs) ?:
	       crypto_shash_export(shash, ipad) ?:
	       crypto_shash_init(shash) ?:
	       crypto_shash_update(shash, opad, bs) ?:
	       crypto_shash_export(shash, opad);
}

static int hmac_export(struct shash_desc *pdesc, void *out)
{
	struct shash_desc *desc = shash_desc_ctx(pdesc);

	return crypto_shash_export(desc, out);
}

static int hmac_import(struct shash_desc *pdesc, const void *in)
{
	struct shash_desc *desc = shash_desc_ctx(pdesc);
	struct hmac_ctx *ctx = hmac_ctx(pdesc->tfm);

	desc->tfm = ctx->hash;

	return crypto_shash_import(desc, in);
}

static int hmac_init(struct shash_desc *pdesc)
{
	return hmac_import(pdesc, crypto_shash_ctx_aligned(pdesc->tfm));
}

static int hmac_update(struct shash_desc *pdesc,
		       const u8 *data, unsigned int nbytes)
{
	struct shash_desc *desc = shash_desc_ctx(pdesc);

	return crypto_shash_update(desc, data, nbytes);
}

static int hmac_final(struct shash_desc *pdesc, u8 *out)
{
	struct crypto_shash *parent = pdesc->tfm;
	int ds = crypto_shash_digestsize(parent);
	int ss = crypto_shash_statesize(parent);
	char *opad = crypto_shash_ctx_aligned(parent) + ss;
	struct shash_desc *desc = shash_desc_ctx(pdesc);

	return crypto_shash_final(desc, out) ?:
	       crypto_shash_import(desc, opad) ?:
	       crypto_shash_finup(desc, out, ds, out);
}

static int hmac_finup(struct shash_desc *pdesc, const u8 *data,
		      unsigned int nbytes, u8 *out)
{

	struct crypto_shash *parent = pdesc->tfm;
	int ds = crypto_shash_digestsize(parent);
	int ss = crypto_shash_statesize(parent);
	char *opad = crypto_shash_ctx_aligned(parent) + ss;
	struct shash_desc *desc = shash_desc_ctx(pdesc);

	return crypto_shash_finup(desc, data, nbytes, out) ?:
	       crypto_shash_import(desc, opad) ?:
	       crypto_shash_finup(desc, out, ds, out);
}

static int hmac_init_tfm(struct crypto_shash *parent)
{
	struct crypto_shash *hash;
	struct shash_instance *inst = shash_alg_instance(parent);
	struct crypto_shash_spawn *spawn = shash_instance_ctx(inst);
	struct hmac_ctx *ctx = hmac_ctx(parent);

	hash = crypto_spawn_shash(spawn);
	if (IS_ERR(hash))
		return PTR_ERR(hash);

	parent->descsize = sizeof(struct shash_desc) +
			   crypto_shash_descsize(hash);

	ctx->hash = hash;
	return 0;
}

static void hmac_exit_tfm(struct crypto_shash *parent)
{
	struct hmac_ctx *ctx = hmac_ctx(parent);
	crypto_free_shash(ctx->hash);
}

static int hmac_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct shash_instance *inst;
	struct crypto_shash_spawn *spawn;
	struct crypto_alg *alg;
	struct shash_alg *salg;
	u32 mask;
	int err;
	int ds;
	int ss;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SHASH, &mask);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;
	spawn = shash_instance_ctx(inst);

	err = crypto_grab_shash(spawn, shash_crypto_instance(inst),
				crypto_attr_alg_name(tb[1]), 0, mask);
	if (err)
		goto err_free_inst;
	salg = crypto_spawn_shash_alg(spawn);
	alg = &salg->base;

	/* The underlying hash algorithm must not require a key */
	err = -EINVAL;
	if (crypto_shash_alg_needs_key(salg))
		goto err_free_inst;

	ds = salg->digestsize;
	ss = salg->statesize;
	if (ds > alg->cra_blocksize ||
	    ss < alg->cra_blocksize)
		goto err_free_inst;

	err = crypto_inst_setname(shash_crypto_instance(inst), tmpl->name, alg);
	if (err)
		goto err_free_inst;

	inst->alg.base.cra_priority = alg->cra_priority;
	inst->alg.base.cra_blocksize = alg->cra_blocksize;
	inst->alg.base.cra_alignmask = alg->cra_alignmask;

	ss = ALIGN(ss, alg->cra_alignmask + 1);
	inst->alg.digestsize = ds;
	inst->alg.statesize = ss;

	inst->alg.base.cra_ctxsize = sizeof(struct hmac_ctx) +
				     ALIGN(ss * 2, crypto_tfm_ctx_alignment());

	inst->alg.init = hmac_init;
	inst->alg.update = hmac_update;
	inst->alg.final = hmac_final;
	inst->alg.finup = hmac_finup;
	inst->alg.export = hmac_export;
	inst->alg.import = hmac_import;
	inst->alg.setkey = hmac_setkey;
	inst->alg.init_tfm = hmac_init_tfm;
	inst->alg.exit_tfm = hmac_exit_tfm;

	inst->free = shash_free_singlespawn_instance;

	err = shash_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		shash_free_singlespawn_instance(inst);
	}
	return err;
}

static struct crypto_template hmac_tmpl = {
	.name = "hmac",
	.create = hmac_create,
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

subsys_initcall(hmac_module_init);
module_exit(hmac_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HMAC hash algorithm");
MODULE_ALIAS_CRYPTO("hmac");
