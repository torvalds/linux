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
#include <linux/err.h>
#include <linux/fips.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

struct hmac_ctx {
	struct crypto_shash *hash;
	/* Contains 'u8 ipad[statesize];', then 'u8 opad[statesize];' */
	u8 pads[];
};

struct ahash_hmac_ctx {
	struct crypto_ahash *hash;
	/* Contains 'u8 ipad[statesize];', then 'u8 opad[statesize];' */
	u8 pads[];
};

static int hmac_setkey(struct crypto_shash *parent,
		       const u8 *inkey, unsigned int keylen)
{
	int bs = crypto_shash_blocksize(parent);
	int ds = crypto_shash_digestsize(parent);
	int ss = crypto_shash_statesize(parent);
	struct hmac_ctx *tctx = crypto_shash_ctx(parent);
	struct crypto_shash *hash = tctx->hash;
	u8 *ipad = &tctx->pads[0];
	u8 *opad = &tctx->pads[ss];
	SHASH_DESC_ON_STACK(shash, hash);
	int err, i;

	if (fips_enabled && (keylen < 112 / 8))
		return -EINVAL;

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

	err = crypto_shash_init(shash) ?:
	      crypto_shash_update(shash, ipad, bs) ?:
	      crypto_shash_export(shash, ipad) ?:
	      crypto_shash_init(shash) ?:
	      crypto_shash_update(shash, opad, bs) ?:
	      crypto_shash_export(shash, opad);
	shash_desc_zero(shash);
	return err;
}

static int hmac_export(struct shash_desc *pdesc, void *out)
{
	struct shash_desc *desc = shash_desc_ctx(pdesc);

	return crypto_shash_export(desc, out);
}

static int hmac_import(struct shash_desc *pdesc, const void *in)
{
	struct shash_desc *desc = shash_desc_ctx(pdesc);
	const struct hmac_ctx *tctx = crypto_shash_ctx(pdesc->tfm);

	desc->tfm = tctx->hash;

	return crypto_shash_import(desc, in);
}

static int hmac_export_core(struct shash_desc *pdesc, void *out)
{
	struct shash_desc *desc = shash_desc_ctx(pdesc);

	return crypto_shash_export_core(desc, out);
}

static int hmac_import_core(struct shash_desc *pdesc, const void *in)
{
	const struct hmac_ctx *tctx = crypto_shash_ctx(pdesc->tfm);
	struct shash_desc *desc = shash_desc_ctx(pdesc);

	desc->tfm = tctx->hash;
	return crypto_shash_import_core(desc, in);
}

static int hmac_init(struct shash_desc *pdesc)
{
	const struct hmac_ctx *tctx = crypto_shash_ctx(pdesc->tfm);

	return hmac_import(pdesc, &tctx->pads[0]);
}

static int hmac_update(struct shash_desc *pdesc,
		       const u8 *data, unsigned int nbytes)
{
	struct shash_desc *desc = shash_desc_ctx(pdesc);

	return crypto_shash_update(desc, data, nbytes);
}

static int hmac_finup(struct shash_desc *pdesc, const u8 *data,
		      unsigned int nbytes, u8 *out)
{

	struct crypto_shash *parent = pdesc->tfm;
	int ds = crypto_shash_digestsize(parent);
	int ss = crypto_shash_statesize(parent);
	const struct hmac_ctx *tctx = crypto_shash_ctx(parent);
	const u8 *opad = &tctx->pads[ss];
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
	struct hmac_ctx *tctx = crypto_shash_ctx(parent);

	hash = crypto_spawn_shash(spawn);
	if (IS_ERR(hash))
		return PTR_ERR(hash);

	tctx->hash = hash;
	return 0;
}

static int hmac_clone_tfm(struct crypto_shash *dst, struct crypto_shash *src)
{
	struct hmac_ctx *sctx = crypto_shash_ctx(src);
	struct hmac_ctx *dctx = crypto_shash_ctx(dst);
	struct crypto_shash *hash;

	hash = crypto_clone_shash(sctx->hash);
	if (IS_ERR(hash))
		return PTR_ERR(hash);

	dctx->hash = hash;
	return 0;
}

static void hmac_exit_tfm(struct crypto_shash *parent)
{
	struct hmac_ctx *tctx = crypto_shash_ctx(parent);

	crypto_free_shash(tctx->hash);
}

static int __hmac_create_shash(struct crypto_template *tmpl,
			       struct rtattr **tb, u32 mask)
{
	struct shash_instance *inst;
	struct crypto_shash_spawn *spawn;
	struct crypto_alg *alg;
	struct shash_alg *salg;
	int err;
	int ds;
	int ss;

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;
	spawn = shash_instance_ctx(inst);

	mask |= CRYPTO_AHASH_ALG_NO_EXPORT_CORE;
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

	err = crypto_inst_setname(shash_crypto_instance(inst), "hmac",
				  "hmac-shash", alg);
	if (err)
		goto err_free_inst;

	inst->alg.base.cra_priority = alg->cra_priority;
	inst->alg.base.cra_blocksize = alg->cra_blocksize;
	inst->alg.base.cra_ctxsize = sizeof(struct hmac_ctx) + (ss * 2);

	inst->alg.digestsize = ds;
	inst->alg.statesize = ss;
	inst->alg.descsize = sizeof(struct shash_desc) + salg->descsize;
	inst->alg.init = hmac_init;
	inst->alg.update = hmac_update;
	inst->alg.finup = hmac_finup;
	inst->alg.export = hmac_export;
	inst->alg.import = hmac_import;
	inst->alg.export_core = hmac_export_core;
	inst->alg.import_core = hmac_import_core;
	inst->alg.setkey = hmac_setkey;
	inst->alg.init_tfm = hmac_init_tfm;
	inst->alg.clone_tfm = hmac_clone_tfm;
	inst->alg.exit_tfm = hmac_exit_tfm;

	inst->free = shash_free_singlespawn_instance;

	err = shash_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		shash_free_singlespawn_instance(inst);
	}
	return err;
}

static int hmac_setkey_ahash(struct crypto_ahash *parent,
			     const u8 *inkey, unsigned int keylen)
{
	struct ahash_hmac_ctx *tctx = crypto_ahash_ctx(parent);
	struct crypto_ahash *fb = crypto_ahash_fb(tctx->hash);
	int ds = crypto_ahash_digestsize(parent);
	int bs = crypto_ahash_blocksize(parent);
	int ss = crypto_ahash_statesize(parent);
	HASH_REQUEST_ON_STACK(req, fb);
	u8 *opad = &tctx->pads[ss];
	u8 *ipad = &tctx->pads[0];
	int err, i;

	if (fips_enabled && (keylen < 112 / 8))
		return -EINVAL;

	ahash_request_set_callback(req, 0, NULL, NULL);

	if (keylen > bs) {
		ahash_request_set_virt(req, inkey, ipad, keylen);
		err = crypto_ahash_digest(req);
		if (err)
			goto out_zero_req;

		keylen = ds;
	} else
		memcpy(ipad, inkey, keylen);

	memset(ipad + keylen, 0, bs - keylen);
	memcpy(opad, ipad, bs);

	for (i = 0; i < bs; i++) {
		ipad[i] ^= HMAC_IPAD_VALUE;
		opad[i] ^= HMAC_OPAD_VALUE;
	}

	ahash_request_set_virt(req, ipad, NULL, bs);
	err = crypto_ahash_init(req) ?:
	      crypto_ahash_update(req) ?:
	      crypto_ahash_export(req, ipad);

	ahash_request_set_virt(req, opad, NULL, bs);
	err = err ?:
	      crypto_ahash_init(req) ?:
	      crypto_ahash_update(req) ?:
	      crypto_ahash_export(req, opad);

out_zero_req:
	HASH_REQUEST_ZERO(req);
	return err;
}

static int hmac_export_ahash(struct ahash_request *preq, void *out)
{
	return crypto_ahash_export(ahash_request_ctx(preq), out);
}

static int hmac_import_ahash(struct ahash_request *preq, const void *in)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(preq);
	struct ahash_hmac_ctx *tctx = crypto_ahash_ctx(tfm);
	struct ahash_request *req = ahash_request_ctx(preq);

	ahash_request_set_tfm(req, tctx->hash);
	return crypto_ahash_import(req, in);
}

static int hmac_export_core_ahash(struct ahash_request *preq, void *out)
{
	return crypto_ahash_export_core(ahash_request_ctx(preq), out);
}

static int hmac_import_core_ahash(struct ahash_request *preq, const void *in)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(preq);
	struct ahash_hmac_ctx *tctx = crypto_ahash_ctx(tfm);
	struct ahash_request *req = ahash_request_ctx(preq);

	ahash_request_set_tfm(req, tctx->hash);
	return crypto_ahash_import_core(req, in);
}

static int hmac_init_ahash(struct ahash_request *preq)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(preq);
	struct ahash_hmac_ctx *tctx = crypto_ahash_ctx(tfm);

	return hmac_import_ahash(preq, &tctx->pads[0]);
}

static int hmac_update_ahash(struct ahash_request *preq)
{
	struct ahash_request *req = ahash_request_ctx(preq);

	ahash_request_set_callback(req, ahash_request_flags(preq),
				   preq->base.complete, preq->base.data);
	if (ahash_request_isvirt(preq))
		ahash_request_set_virt(req, preq->svirt, NULL, preq->nbytes);
	else
		ahash_request_set_crypt(req, preq->src, NULL, preq->nbytes);
	return crypto_ahash_update(req);
}

static int hmac_finup_finish(struct ahash_request *preq, unsigned int mask)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(preq);
	struct ahash_request *req = ahash_request_ctx(preq);
	struct ahash_hmac_ctx *tctx = crypto_ahash_ctx(tfm);
	int ds = crypto_ahash_digestsize(tfm);
	int ss = crypto_ahash_statesize(tfm);
	const u8 *opad = &tctx->pads[ss];

	ahash_request_set_callback(req, ahash_request_flags(preq) & ~mask,
				   preq->base.complete, preq->base.data);
	ahash_request_set_virt(req, preq->result, preq->result, ds);
	return crypto_ahash_import(req, opad) ?:
	       crypto_ahash_finup(req);

}

static void hmac_finup_done(void *data, int err)
{
	struct ahash_request *preq = data;

	if (err)
		goto out;

	err = hmac_finup_finish(preq, CRYPTO_TFM_REQ_MAY_SLEEP);
	if (err == -EINPROGRESS || err == -EBUSY)
		return;

out:
	ahash_request_complete(preq, err);
}

static int hmac_finup_ahash(struct ahash_request *preq)
{
	struct ahash_request *req = ahash_request_ctx(preq);

	ahash_request_set_callback(req, ahash_request_flags(preq),
				   hmac_finup_done, preq);
	if (ahash_request_isvirt(preq))
		ahash_request_set_virt(req, preq->svirt, preq->result,
				       preq->nbytes);
	else
		ahash_request_set_crypt(req, preq->src, preq->result,
					preq->nbytes);
	return crypto_ahash_finup(req) ?:
	       hmac_finup_finish(preq, 0);
}

static int hmac_digest_ahash(struct ahash_request *preq)
{
	return hmac_init_ahash(preq) ?:
	       hmac_finup_ahash(preq);
}

static int hmac_init_ahash_tfm(struct crypto_ahash *parent)
{
	struct ahash_instance *inst = ahash_alg_instance(parent);
	struct ahash_hmac_ctx *tctx = crypto_ahash_ctx(parent);
	struct crypto_ahash *hash;

	hash = crypto_spawn_ahash(ahash_instance_ctx(inst));
	if (IS_ERR(hash))
		return PTR_ERR(hash);

	if (crypto_ahash_reqsize(parent) < sizeof(struct ahash_request) +
					   crypto_ahash_reqsize(hash))
		return -EINVAL;

	tctx->hash = hash;
	return 0;
}

static int hmac_clone_ahash_tfm(struct crypto_ahash *dst,
				struct crypto_ahash *src)
{
	struct ahash_hmac_ctx *sctx = crypto_ahash_ctx(src);
	struct ahash_hmac_ctx *dctx = crypto_ahash_ctx(dst);
	struct crypto_ahash *hash;

	hash = crypto_clone_ahash(sctx->hash);
	if (IS_ERR(hash))
		return PTR_ERR(hash);

	dctx->hash = hash;
	return 0;
}

static void hmac_exit_ahash_tfm(struct crypto_ahash *parent)
{
	struct ahash_hmac_ctx *tctx = crypto_ahash_ctx(parent);

	crypto_free_ahash(tctx->hash);
}

static int hmac_create_ahash(struct crypto_template *tmpl, struct rtattr **tb,
			     u32 mask)
{
	struct crypto_ahash_spawn *spawn;
	struct ahash_instance *inst;
	struct crypto_alg *alg;
	struct hash_alg_common *halg;
	int ds, ss, err;

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;
	spawn = ahash_instance_ctx(inst);

	mask |= CRYPTO_AHASH_ALG_NO_EXPORT_CORE;
	err = crypto_grab_ahash(spawn, ahash_crypto_instance(inst),
				crypto_attr_alg_name(tb[1]), 0, mask);
	if (err)
		goto err_free_inst;
	halg = crypto_spawn_ahash_alg(spawn);
	alg = &halg->base;

	/* The underlying hash algorithm must not require a key */
	err = -EINVAL;
	if (crypto_hash_alg_needs_key(halg))
		goto err_free_inst;

	ds = halg->digestsize;
	ss = halg->statesize;
	if (ds > alg->cra_blocksize || ss < alg->cra_blocksize)
		goto err_free_inst;

	err = crypto_inst_setname(ahash_crypto_instance(inst), tmpl->name, alg);
	if (err)
		goto err_free_inst;

	inst->alg.halg.base.cra_flags = alg->cra_flags &
					CRYPTO_ALG_INHERITED_FLAGS;
	inst->alg.halg.base.cra_flags |= CRYPTO_ALG_REQ_VIRT;
	inst->alg.halg.base.cra_priority = alg->cra_priority + 100;
	inst->alg.halg.base.cra_blocksize = alg->cra_blocksize;
	inst->alg.halg.base.cra_ctxsize = sizeof(struct ahash_hmac_ctx) +
					  (ss * 2);
	inst->alg.halg.base.cra_reqsize = sizeof(struct ahash_request) +
					  alg->cra_reqsize;

	inst->alg.halg.digestsize = ds;
	inst->alg.halg.statesize = ss;
	inst->alg.init = hmac_init_ahash;
	inst->alg.update = hmac_update_ahash;
	inst->alg.finup = hmac_finup_ahash;
	inst->alg.digest = hmac_digest_ahash;
	inst->alg.export = hmac_export_ahash;
	inst->alg.import = hmac_import_ahash;
	inst->alg.export_core = hmac_export_core_ahash;
	inst->alg.import_core = hmac_import_core_ahash;
	inst->alg.setkey = hmac_setkey_ahash;
	inst->alg.init_tfm = hmac_init_ahash_tfm;
	inst->alg.clone_tfm = hmac_clone_ahash_tfm;
	inst->alg.exit_tfm = hmac_exit_ahash_tfm;

	inst->free = ahash_free_singlespawn_instance;

	err = ahash_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		ahash_free_singlespawn_instance(inst);
	}
	return err;
}

static int hmac_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct crypto_attr_type *algt;
	u32 mask;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	mask = crypto_algt_inherited_mask(algt);

	if (!((algt->type ^ CRYPTO_ALG_TYPE_AHASH) &
	      algt->mask & CRYPTO_ALG_TYPE_MASK))
		return hmac_create_ahash(tmpl, tb, mask);

	if ((algt->type ^ CRYPTO_ALG_TYPE_SHASH) &
	    algt->mask & CRYPTO_ALG_TYPE_MASK)
		return -EINVAL;

	return __hmac_create_shash(tmpl, tb, mask);
}

static int hmac_create_shash(struct crypto_template *tmpl, struct rtattr **tb)
{
	u32 mask;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SHASH, &mask);
	if (err)
		return err == -EINVAL ? -ENOENT : err;

	return __hmac_create_shash(tmpl, tb, mask);
}

static struct crypto_template hmac_tmpls[] = {
	{
		.name = "hmac",
		.create = hmac_create,
		.module = THIS_MODULE,
	},
	{
		.name = "hmac-shash",
		.create = hmac_create_shash,
		.module = THIS_MODULE,
	},
};

static int __init hmac_module_init(void)
{
	return crypto_register_templates(hmac_tmpls, ARRAY_SIZE(hmac_tmpls));
}

static void __exit hmac_module_exit(void)
{
	crypto_unregister_templates(hmac_tmpls, ARRAY_SIZE(hmac_tmpls));
}

module_init(hmac_module_init);
module_exit(hmac_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HMAC hash algorithm");
MODULE_ALIAS_CRYPTO("hmac");
