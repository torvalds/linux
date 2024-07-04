// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Synchronous Cryptographic Hash operations.
 *
 * Copyright (c) 2008 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/scatterwalk.h>
#include <linux/cryptouser.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <net/netlink.h>

#include "hash.h"

int shash_no_setkey(struct crypto_shash *tfm, const u8 *key,
		    unsigned int keylen)
{
	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(shash_no_setkey);

static void shash_set_needkey(struct crypto_shash *tfm, struct shash_alg *alg)
{
	if (crypto_shash_alg_needs_key(alg))
		crypto_shash_set_flags(tfm, CRYPTO_TFM_NEED_KEY);
}

int crypto_shash_setkey(struct crypto_shash *tfm, const u8 *key,
			unsigned int keylen)
{
	struct shash_alg *shash = crypto_shash_alg(tfm);
	int err;

	err = shash->setkey(tfm, key, keylen);
	if (unlikely(err)) {
		shash_set_needkey(tfm, shash);
		return err;
	}

	crypto_shash_clear_flags(tfm, CRYPTO_TFM_NEED_KEY);
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_shash_setkey);

int crypto_shash_update(struct shash_desc *desc, const u8 *data,
			unsigned int len)
{
	return crypto_shash_alg(desc->tfm)->update(desc, data, len);
}
EXPORT_SYMBOL_GPL(crypto_shash_update);

int crypto_shash_final(struct shash_desc *desc, u8 *out)
{
	return crypto_shash_alg(desc->tfm)->final(desc, out);
}
EXPORT_SYMBOL_GPL(crypto_shash_final);

static int shash_default_finup(struct shash_desc *desc, const u8 *data,
			       unsigned int len, u8 *out)
{
	struct shash_alg *shash = crypto_shash_alg(desc->tfm);

	return shash->update(desc, data, len) ?:
	       shash->final(desc, out);
}

int crypto_shash_finup(struct shash_desc *desc, const u8 *data,
		       unsigned int len, u8 *out)
{
	return crypto_shash_alg(desc->tfm)->finup(desc, data, len, out);
}
EXPORT_SYMBOL_GPL(crypto_shash_finup);

static int shash_default_digest(struct shash_desc *desc, const u8 *data,
				unsigned int len, u8 *out)
{
	struct shash_alg *shash = crypto_shash_alg(desc->tfm);

	return shash->init(desc) ?:
	       shash->finup(desc, data, len, out);
}

int crypto_shash_digest(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out)
{
	struct crypto_shash *tfm = desc->tfm;

	if (crypto_shash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;

	return crypto_shash_alg(tfm)->digest(desc, data, len, out);
}
EXPORT_SYMBOL_GPL(crypto_shash_digest);

int crypto_shash_tfm_digest(struct crypto_shash *tfm, const u8 *data,
			    unsigned int len, u8 *out)
{
	SHASH_DESC_ON_STACK(desc, tfm);
	int err;

	desc->tfm = tfm;

	err = crypto_shash_digest(desc, data, len, out);

	shash_desc_zero(desc);

	return err;
}
EXPORT_SYMBOL_GPL(crypto_shash_tfm_digest);

int crypto_shash_export(struct shash_desc *desc, void *out)
{
	struct crypto_shash *tfm = desc->tfm;
	struct shash_alg *shash = crypto_shash_alg(tfm);

	if (shash->export)
		return shash->export(desc, out);

	memcpy(out, shash_desc_ctx(desc), crypto_shash_descsize(tfm));
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_shash_export);

int crypto_shash_import(struct shash_desc *desc, const void *in)
{
	struct crypto_shash *tfm = desc->tfm;
	struct shash_alg *shash = crypto_shash_alg(tfm);

	if (crypto_shash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;

	if (shash->import)
		return shash->import(desc, in);

	memcpy(shash_desc_ctx(desc), in, crypto_shash_descsize(tfm));
	return 0;
}
EXPORT_SYMBOL_GPL(crypto_shash_import);

static void crypto_shash_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_shash *hash = __crypto_shash_cast(tfm);
	struct shash_alg *alg = crypto_shash_alg(hash);

	alg->exit_tfm(hash);
}

static int crypto_shash_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_shash *hash = __crypto_shash_cast(tfm);
	struct shash_alg *alg = crypto_shash_alg(hash);
	int err;

	hash->descsize = alg->descsize;

	shash_set_needkey(hash, alg);

	if (alg->exit_tfm)
		tfm->exit = crypto_shash_exit_tfm;

	if (!alg->init_tfm)
		return 0;

	err = alg->init_tfm(hash);
	if (err)
		return err;

	/* ->init_tfm() may have increased the descsize. */
	if (WARN_ON_ONCE(hash->descsize > HASH_MAX_DESCSIZE)) {
		if (alg->exit_tfm)
			alg->exit_tfm(hash);
		return -EINVAL;
	}

	return 0;
}

static void crypto_shash_free_instance(struct crypto_instance *inst)
{
	struct shash_instance *shash = shash_instance(inst);

	shash->free(shash);
}

static int __maybe_unused crypto_shash_report(
	struct sk_buff *skb, struct crypto_alg *alg)
{
	struct crypto_report_hash rhash;
	struct shash_alg *salg = __crypto_shash_alg(alg);

	memset(&rhash, 0, sizeof(rhash));

	strscpy(rhash.type, "shash", sizeof(rhash.type));

	rhash.blocksize = alg->cra_blocksize;
	rhash.digestsize = salg->digestsize;

	return nla_put(skb, CRYPTOCFGA_REPORT_HASH, sizeof(rhash), &rhash);
}

static void crypto_shash_show(struct seq_file *m, struct crypto_alg *alg)
	__maybe_unused;
static void crypto_shash_show(struct seq_file *m, struct crypto_alg *alg)
{
	struct shash_alg *salg = __crypto_shash_alg(alg);

	seq_printf(m, "type         : shash\n");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "digestsize   : %u\n", salg->digestsize);
}

const struct crypto_type crypto_shash_type = {
	.extsize = crypto_alg_extsize,
	.init_tfm = crypto_shash_init_tfm,
	.free = crypto_shash_free_instance,
#ifdef CONFIG_PROC_FS
	.show = crypto_shash_show,
#endif
#if IS_ENABLED(CONFIG_CRYPTO_USER)
	.report = crypto_shash_report,
#endif
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_MASK,
	.type = CRYPTO_ALG_TYPE_SHASH,
	.tfmsize = offsetof(struct crypto_shash, base),
};

int crypto_grab_shash(struct crypto_shash_spawn *spawn,
		      struct crypto_instance *inst,
		      const char *name, u32 type, u32 mask)
{
	spawn->base.frontend = &crypto_shash_type;
	return crypto_grab_spawn(&spawn->base, inst, name, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_grab_shash);

struct crypto_shash *crypto_alloc_shash(const char *alg_name, u32 type,
					u32 mask)
{
	return crypto_alloc_tfm(alg_name, &crypto_shash_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_alloc_shash);

int crypto_has_shash(const char *alg_name, u32 type, u32 mask)
{
	return crypto_type_has_alg(alg_name, &crypto_shash_type, type, mask);
}
EXPORT_SYMBOL_GPL(crypto_has_shash);

struct crypto_shash *crypto_clone_shash(struct crypto_shash *hash)
{
	struct crypto_tfm *tfm = crypto_shash_tfm(hash);
	struct shash_alg *alg = crypto_shash_alg(hash);
	struct crypto_shash *nhash;
	int err;

	if (!crypto_shash_alg_has_setkey(alg)) {
		tfm = crypto_tfm_get(tfm);
		if (IS_ERR(tfm))
			return ERR_CAST(tfm);

		return hash;
	}

	if (!alg->clone_tfm && (alg->init_tfm || alg->base.cra_init))
		return ERR_PTR(-ENOSYS);

	nhash = crypto_clone_tfm(&crypto_shash_type, tfm);
	if (IS_ERR(nhash))
		return nhash;

	nhash->descsize = hash->descsize;

	if (alg->clone_tfm) {
		err = alg->clone_tfm(nhash, hash);
		if (err) {
			crypto_free_shash(nhash);
			return ERR_PTR(err);
		}
	}

	return nhash;
}
EXPORT_SYMBOL_GPL(crypto_clone_shash);

int hash_prepare_alg(struct hash_alg_common *alg)
{
	struct crypto_alg *base = &alg->base;

	if (alg->digestsize > HASH_MAX_DIGESTSIZE)
		return -EINVAL;

	/* alignmask is not useful for hashes, so it is not supported. */
	if (base->cra_alignmask)
		return -EINVAL;

	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;

	return 0;
}

static int shash_prepare_alg(struct shash_alg *alg)
{
	struct crypto_alg *base = &alg->halg.base;
	int err;

	if (alg->descsize > HASH_MAX_DESCSIZE)
		return -EINVAL;

	if ((alg->export && !alg->import) || (alg->import && !alg->export))
		return -EINVAL;

	err = hash_prepare_alg(&alg->halg);
	if (err)
		return err;

	base->cra_type = &crypto_shash_type;
	base->cra_flags |= CRYPTO_ALG_TYPE_SHASH;

	/*
	 * Handle missing optional functions.  For each one we can either
	 * install a default here, or we can leave the pointer as NULL and check
	 * the pointer for NULL in crypto_shash_*(), avoiding an indirect call
	 * when the default behavior is desired.  For ->finup and ->digest we
	 * install defaults, since for optimal performance algorithms should
	 * implement these anyway.  On the other hand, for ->import and
	 * ->export the common case and best performance comes from the simple
	 * memcpy of the shash_desc_ctx, so when those pointers are NULL we
	 * leave them NULL and provide the memcpy with no indirect call.
	 */
	if (!alg->finup)
		alg->finup = shash_default_finup;
	if (!alg->digest)
		alg->digest = shash_default_digest;
	if (!alg->export)
		alg->halg.statesize = alg->descsize;
	if (!alg->setkey)
		alg->setkey = shash_no_setkey;

	return 0;
}

int crypto_register_shash(struct shash_alg *alg)
{
	struct crypto_alg *base = &alg->base;
	int err;

	err = shash_prepare_alg(alg);
	if (err)
		return err;

	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_shash);

void crypto_unregister_shash(struct shash_alg *alg)
{
	crypto_unregister_alg(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_shash);

int crypto_register_shashes(struct shash_alg *algs, int count)
{
	int i, ret;

	for (i = 0; i < count; i++) {
		ret = crypto_register_shash(&algs[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	for (--i; i >= 0; --i)
		crypto_unregister_shash(&algs[i]);

	return ret;
}
EXPORT_SYMBOL_GPL(crypto_register_shashes);

void crypto_unregister_shashes(struct shash_alg *algs, int count)
{
	int i;

	for (i = count - 1; i >= 0; --i)
		crypto_unregister_shash(&algs[i]);
}
EXPORT_SYMBOL_GPL(crypto_unregister_shashes);

int shash_register_instance(struct crypto_template *tmpl,
			    struct shash_instance *inst)
{
	int err;

	if (WARN_ON(!inst->free))
		return -EINVAL;

	err = shash_prepare_alg(&inst->alg);
	if (err)
		return err;

	return crypto_register_instance(tmpl, shash_crypto_instance(inst));
}
EXPORT_SYMBOL_GPL(shash_register_instance);

void shash_free_singlespawn_instance(struct shash_instance *inst)
{
	crypto_drop_spawn(shash_instance_ctx(inst));
	kfree(inst);
}
EXPORT_SYMBOL_GPL(shash_free_singlespawn_instance);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synchronous cryptographic hash type");
