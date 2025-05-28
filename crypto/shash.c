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

static inline bool crypto_shash_block_only(struct crypto_shash *tfm)
{
	return crypto_shash_alg(tfm)->base.cra_flags &
	       CRYPTO_AHASH_ALG_BLOCK_ONLY;
}

static inline bool crypto_shash_final_nonzero(struct crypto_shash *tfm)
{
	return crypto_shash_alg(tfm)->base.cra_flags &
	       CRYPTO_AHASH_ALG_FINAL_NONZERO;
}

static inline bool crypto_shash_finup_max(struct crypto_shash *tfm)
{
	return crypto_shash_alg(tfm)->base.cra_flags &
	       CRYPTO_AHASH_ALG_FINUP_MAX;
}

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

static int __crypto_shash_init(struct shash_desc *desc)
{
	struct crypto_shash *tfm = desc->tfm;

	if (crypto_shash_block_only(tfm)) {
		u8 *buf = shash_desc_ctx(desc);

		buf += crypto_shash_descsize(tfm) - 1;
		*buf = 0;
	}

	return crypto_shash_alg(tfm)->init(desc);
}

int crypto_shash_init(struct shash_desc *desc)
{
	if (crypto_shash_get_flags(desc->tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;
	return __crypto_shash_init(desc);
}
EXPORT_SYMBOL_GPL(crypto_shash_init);

static int shash_default_finup(struct shash_desc *desc, const u8 *data,
			       unsigned int len, u8 *out)
{
	struct shash_alg *shash = crypto_shash_alg(desc->tfm);

	return shash->update(desc, data, len) ?:
	       shash->final(desc, out);
}

static int crypto_shash_op_and_zero(
	int (*op)(struct shash_desc *desc, const u8 *data,
		  unsigned int len, u8 *out),
	struct shash_desc *desc, const u8 *data, unsigned int len, u8 *out)
{
	int err;

	err = op(desc, data, len, out);
	memset(shash_desc_ctx(desc), 0, crypto_shash_descsize(desc->tfm));
	return err;
}

int crypto_shash_finup(struct shash_desc *restrict desc, const u8 *data,
		       unsigned int len, u8 *restrict out)
{
	struct crypto_shash *tfm = desc->tfm;
	u8 *blenp = shash_desc_ctx(desc);
	bool finup_max, nonzero;
	unsigned int bs;
	int err;
	u8 *buf;

	if (!crypto_shash_block_only(tfm)) {
		if (out)
			goto finup;
		return crypto_shash_alg(tfm)->update(desc, data, len);
	}

	finup_max = out && crypto_shash_finup_max(tfm);

	/* Retain extra block for final nonzero algorithms. */
	nonzero = crypto_shash_final_nonzero(tfm);

	/*
	 * The partial block buffer follows the algorithm desc context.
	 * The byte following that contains the length.
	 */
	blenp += crypto_shash_descsize(tfm) - 1;
	bs = crypto_shash_blocksize(tfm);
	buf = blenp - bs;

	if (likely(!*blenp && finup_max))
		goto finup;

	while ((*blenp + len) >= bs + nonzero) {
		unsigned int nbytes = len - nonzero;
		const u8 *src = data;

		if (*blenp) {
			memcpy(buf + *blenp, data, bs - *blenp);
			nbytes = bs;
			src = buf;
		}

		err = crypto_shash_alg(tfm)->update(desc, src, nbytes);
		if (err < 0)
			return err;

		data += nbytes - err - *blenp;
		len -= nbytes - err - *blenp;
		*blenp = 0;
	}

	if (*blenp || !out) {
		memcpy(buf + *blenp, data, len);
		*blenp += len;
		if (!out)
			return 0;
		data = buf;
		len = *blenp;
	}

finup:
	return crypto_shash_op_and_zero(crypto_shash_alg(tfm)->finup, desc,
					data, len, out);
}
EXPORT_SYMBOL_GPL(crypto_shash_finup);

static int shash_default_digest(struct shash_desc *desc, const u8 *data,
				unsigned int len, u8 *out)
{
	return __crypto_shash_init(desc) ?:
	       crypto_shash_finup(desc, data, len, out);
}

int crypto_shash_digest(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out)
{
	struct crypto_shash *tfm = desc->tfm;

	if (crypto_shash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;

	return crypto_shash_op_and_zero(crypto_shash_alg(tfm)->digest, desc,
					data, len, out);
}
EXPORT_SYMBOL_GPL(crypto_shash_digest);

int crypto_shash_tfm_digest(struct crypto_shash *tfm, const u8 *data,
			    unsigned int len, u8 *out)
{
	SHASH_DESC_ON_STACK(desc, tfm);

	desc->tfm = tfm;
	return crypto_shash_digest(desc, data, len, out);
}
EXPORT_SYMBOL_GPL(crypto_shash_tfm_digest);

static int __crypto_shash_export(struct shash_desc *desc, void *out,
				 int (*export)(struct shash_desc *desc,
					       void *out))
{
	struct crypto_shash *tfm = desc->tfm;
	u8 *buf = shash_desc_ctx(desc);
	unsigned int plen, ss;

	plen = crypto_shash_blocksize(tfm) + 1;
	ss = crypto_shash_statesize(tfm);
	if (crypto_shash_block_only(tfm))
		ss -= plen;
	if (!export) {
		memcpy(out, buf, ss);
		return 0;
	}

	return export(desc, out);
}

int crypto_shash_export_core(struct shash_desc *desc, void *out)
{
	return __crypto_shash_export(desc, out,
				     crypto_shash_alg(desc->tfm)->export_core);
}
EXPORT_SYMBOL_GPL(crypto_shash_export_core);

int crypto_shash_export(struct shash_desc *desc, void *out)
{
	struct crypto_shash *tfm = desc->tfm;

	if (crypto_shash_block_only(tfm)) {
		unsigned int plen = crypto_shash_blocksize(tfm) + 1;
		unsigned int descsize = crypto_shash_descsize(tfm);
		unsigned int ss = crypto_shash_statesize(tfm);
		u8 *buf = shash_desc_ctx(desc);

		memcpy(out + ss - plen, buf + descsize - plen, plen);
	}
	return __crypto_shash_export(desc, out, crypto_shash_alg(tfm)->export);
}
EXPORT_SYMBOL_GPL(crypto_shash_export);

static int __crypto_shash_import(struct shash_desc *desc, const void *in,
				 int (*import)(struct shash_desc *desc,
					       const void *in))
{
	struct crypto_shash *tfm = desc->tfm;
	unsigned int descsize, plen, ss;
	u8 *buf = shash_desc_ctx(desc);

	if (crypto_shash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;

	ss = crypto_shash_statesize(tfm);
	if (crypto_shash_block_only(tfm)) {
		plen = crypto_shash_blocksize(tfm) + 1;
		ss -= plen;
		descsize = crypto_shash_descsize(tfm);
		buf[descsize - 1] = 0;
	}
	if (!import) {
		memcpy(buf, in, ss);
		return 0;
	}

	return import(desc, in);
}

int crypto_shash_import_core(struct shash_desc *desc, const void *in)
{
	return __crypto_shash_import(desc, in,
				     crypto_shash_alg(desc->tfm)->import_core);
}
EXPORT_SYMBOL_GPL(crypto_shash_import_core);

int crypto_shash_import(struct shash_desc *desc, const void *in)
{
	struct crypto_shash *tfm = desc->tfm;
	int err;

	err = __crypto_shash_import(desc, in, crypto_shash_alg(tfm)->import);
	if (crypto_shash_block_only(tfm)) {
		unsigned int plen = crypto_shash_blocksize(tfm) + 1;
		unsigned int descsize = crypto_shash_descsize(tfm);
		unsigned int ss = crypto_shash_statesize(tfm);
		u8 *buf = shash_desc_ctx(desc);

		memcpy(buf + descsize - plen, in + ss - plen, plen);
		if (buf[descsize - 1] >= plen)
			err = -EOVERFLOW;
	}
	return err;
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

	shash_set_needkey(hash, alg);

	if (alg->exit_tfm)
		tfm->exit = crypto_shash_exit_tfm;

	if (!alg->init_tfm)
		return 0;

	return alg->init_tfm(hash);
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
	.algsize = offsetof(struct shash_alg, base),
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

	if (alg->clone_tfm) {
		err = alg->clone_tfm(nhash, hash);
		if (err) {
			crypto_free_shash(nhash);
			return ERR_PTR(err);
		}
	}

	if (alg->exit_tfm)
		crypto_shash_tfm(nhash)->exit = crypto_shash_exit_tfm;

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

static int shash_default_export_core(struct shash_desc *desc, void *out)
{
	return -ENOSYS;
}

static int shash_default_import_core(struct shash_desc *desc, const void *in)
{
	return -ENOSYS;
}

static int shash_prepare_alg(struct shash_alg *alg)
{
	struct crypto_alg *base = &alg->halg.base;
	int err;

	if ((alg->export && !alg->import) || (alg->import && !alg->export))
		return -EINVAL;

	err = hash_prepare_alg(&alg->halg);
	if (err)
		return err;

	base->cra_type = &crypto_shash_type;
	base->cra_flags |= CRYPTO_ALG_TYPE_SHASH;
	base->cra_flags |= CRYPTO_ALG_REQ_VIRT;

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
	if (!alg->export && !alg->halg.statesize)
		alg->halg.statesize = alg->descsize;
	if (!alg->setkey)
		alg->setkey = shash_no_setkey;

	if (base->cra_flags & CRYPTO_AHASH_ALG_BLOCK_ONLY) {
		BUILD_BUG_ON(MAX_ALGAPI_BLOCKSIZE >= 256);
		alg->descsize += base->cra_blocksize + 1;
		alg->statesize += base->cra_blocksize + 1;
		alg->export_core = alg->export;
		alg->import_core = alg->import;
	} else if (!alg->export_core || !alg->import_core) {
		alg->export_core = shash_default_export_core;
		alg->import_core = shash_default_import_core;
		base->cra_flags |= CRYPTO_AHASH_ALG_NO_EXPORT_CORE;
	}

	if (alg->descsize > HASH_MAX_DESCSIZE)
		return -EINVAL;
	if (alg->statesize > HASH_MAX_STATESIZE)
		return -EINVAL;

	base->cra_reqsize = sizeof(struct shash_desc) + alg->descsize;

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
