/*
 * Synchronous Cryptographic Hash operations.
 *
 * Copyright (c) 2008 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/scatterwalk.h>
#include <crypto/internal/hash.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/seq_file.h>

static const struct crypto_type crypto_shash_type;

static inline struct crypto_shash *__crypto_shash_cast(struct crypto_tfm *tfm)
{
	return container_of(tfm, struct crypto_shash, base);
}

#include "internal.h"

static int shash_setkey_unaligned(struct crypto_shash *tfm, const u8 *key,
				  unsigned int keylen)
{
	struct shash_alg *shash = crypto_shash_alg(tfm);
	unsigned long alignmask = crypto_shash_alignmask(tfm);
	unsigned long absize;
	u8 *buffer, *alignbuffer;
	int err;

	absize = keylen + (alignmask & ~(CRYPTO_MINALIGN - 1));
	buffer = kmalloc(absize, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	alignbuffer = (u8 *)ALIGN((unsigned long)buffer, alignmask + 1);
	memcpy(alignbuffer, key, keylen);
	err = shash->setkey(tfm, alignbuffer, keylen);
	memset(alignbuffer, 0, keylen);
	kfree(buffer);
	return err;
}

int crypto_shash_setkey(struct crypto_shash *tfm, const u8 *key,
			unsigned int keylen)
{
	struct shash_alg *shash = crypto_shash_alg(tfm);
	unsigned long alignmask = crypto_shash_alignmask(tfm);

	if (!shash->setkey)
		return -ENOSYS;

	if ((unsigned long)key & alignmask)
		return shash_setkey_unaligned(tfm, key, keylen);

	return shash->setkey(tfm, key, keylen);
}
EXPORT_SYMBOL_GPL(crypto_shash_setkey);

static inline unsigned int shash_align_buffer_size(unsigned len,
						   unsigned long mask)
{
	return len + (mask & ~(__alignof__(u8 __attribute__ ((aligned))) - 1));
}

static int shash_update_unaligned(struct shash_desc *desc, const u8 *data,
				  unsigned int len)
{
	struct crypto_shash *tfm = desc->tfm;
	struct shash_alg *shash = crypto_shash_alg(tfm);
	unsigned long alignmask = crypto_shash_alignmask(tfm);
	unsigned int unaligned_len = alignmask + 1 -
				     ((unsigned long)data & alignmask);
	u8 buf[shash_align_buffer_size(unaligned_len, alignmask)]
		__attribute__ ((aligned));

	memcpy(buf, data, unaligned_len);

	return shash->update(desc, buf, unaligned_len) ?:
	       shash->update(desc, data + unaligned_len, len - unaligned_len);
}

int crypto_shash_update(struct shash_desc *desc, const u8 *data,
			unsigned int len)
{
	struct crypto_shash *tfm = desc->tfm;
	struct shash_alg *shash = crypto_shash_alg(tfm);
	unsigned long alignmask = crypto_shash_alignmask(tfm);

	if ((unsigned long)data & alignmask)
		return shash_update_unaligned(desc, data, len);

	return shash->update(desc, data, len);
}
EXPORT_SYMBOL_GPL(crypto_shash_update);

static int shash_final_unaligned(struct shash_desc *desc, u8 *out)
{
	struct crypto_shash *tfm = desc->tfm;
	unsigned long alignmask = crypto_shash_alignmask(tfm);
	struct shash_alg *shash = crypto_shash_alg(tfm);
	unsigned int ds = crypto_shash_digestsize(tfm);
	u8 buf[shash_align_buffer_size(ds, alignmask)]
		__attribute__ ((aligned));
	int err;

	err = shash->final(desc, buf);
	memcpy(out, buf, ds);
	return err;
}

int crypto_shash_final(struct shash_desc *desc, u8 *out)
{
	struct crypto_shash *tfm = desc->tfm;
	struct shash_alg *shash = crypto_shash_alg(tfm);
	unsigned long alignmask = crypto_shash_alignmask(tfm);

	if ((unsigned long)out & alignmask)
		return shash_final_unaligned(desc, out);

	return shash->final(desc, out);
}
EXPORT_SYMBOL_GPL(crypto_shash_final);

static int shash_finup_unaligned(struct shash_desc *desc, const u8 *data,
				 unsigned int len, u8 *out)
{
	return crypto_shash_update(desc, data, len) ?:
	       crypto_shash_final(desc, out);
}

int crypto_shash_finup(struct shash_desc *desc, const u8 *data,
		       unsigned int len, u8 *out)
{
	struct crypto_shash *tfm = desc->tfm;
	struct shash_alg *shash = crypto_shash_alg(tfm);
	unsigned long alignmask = crypto_shash_alignmask(tfm);

	if (((unsigned long)data | (unsigned long)out) & alignmask ||
	    !shash->finup)
		return shash_finup_unaligned(desc, data, len, out);

	return shash->finup(desc, data, len, out);
}
EXPORT_SYMBOL_GPL(crypto_shash_finup);

static int shash_digest_unaligned(struct shash_desc *desc, const u8 *data,
				  unsigned int len, u8 *out)
{
	return crypto_shash_init(desc) ?:
	       crypto_shash_update(desc, data, len) ?:
	       crypto_shash_final(desc, out);
}

int crypto_shash_digest(struct shash_desc *desc, const u8 *data,
			unsigned int len, u8 *out)
{
	struct crypto_shash *tfm = desc->tfm;
	struct shash_alg *shash = crypto_shash_alg(tfm);
	unsigned long alignmask = crypto_shash_alignmask(tfm);

	if (((unsigned long)data | (unsigned long)out) & alignmask ||
	    !shash->digest)
		return shash_digest_unaligned(desc, data, len, out);

	return shash->digest(desc, data, len, out);
}
EXPORT_SYMBOL_GPL(crypto_shash_digest);

int crypto_shash_import(struct shash_desc *desc, const u8 *in)
{
	struct crypto_shash *tfm = desc->tfm;
	struct shash_alg *alg = crypto_shash_alg(tfm);

	memcpy(shash_desc_ctx(desc), in, crypto_shash_descsize(tfm));

	if (alg->reinit)
		alg->reinit(desc);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_shash_import);

static int shash_async_setkey(struct crypto_ahash *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct crypto_shash **ctx = crypto_ahash_ctx(tfm);

	return crypto_shash_setkey(*ctx, key, keylen);
}

static int shash_async_init(struct ahash_request *req)
{
	struct crypto_shash **ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct shash_desc *desc = ahash_request_ctx(req);

	desc->tfm = *ctx;
	desc->flags = req->base.flags;

	return crypto_shash_init(desc);
}

static int shash_async_update(struct ahash_request *req)
{
	struct shash_desc *desc = ahash_request_ctx(req);
	struct crypto_hash_walk walk;
	int nbytes;

	for (nbytes = crypto_hash_walk_first(req, &walk); nbytes > 0;
	     nbytes = crypto_hash_walk_done(&walk, nbytes))
		nbytes = crypto_shash_update(desc, walk.data, nbytes);

	return nbytes;
}

static int shash_async_final(struct ahash_request *req)
{
	return crypto_shash_final(ahash_request_ctx(req), req->result);
}

static int shash_async_digest(struct ahash_request *req)
{
	struct scatterlist *sg = req->src;
	unsigned int offset = sg->offset;
	unsigned int nbytes = req->nbytes;
	int err;

	if (nbytes < min(sg->length, ((unsigned int)(PAGE_SIZE)) - offset)) {
		struct crypto_shash **ctx =
			crypto_ahash_ctx(crypto_ahash_reqtfm(req));
		struct shash_desc *desc = ahash_request_ctx(req);
		void *data;

		desc->tfm = *ctx;
		desc->flags = req->base.flags;

		data = crypto_kmap(sg_page(sg), 0);
		err = crypto_shash_digest(desc, data + offset, nbytes,
					  req->result);
		crypto_kunmap(data, 0);
		crypto_yield(desc->flags);
		goto out;
	}

	err = shash_async_init(req);
	if (err)
		goto out;

	err = shash_async_update(req);
	if (err)
		goto out;

	err = shash_async_final(req);

out:
	return err;
}

static void crypto_exit_shash_ops_async(struct crypto_tfm *tfm)
{
	struct crypto_shash **ctx = crypto_tfm_ctx(tfm);

	crypto_free_shash(*ctx);
}

static int crypto_init_shash_ops_async(struct crypto_tfm *tfm)
{
	struct crypto_alg *calg = tfm->__crt_alg;
	struct shash_alg *alg = __crypto_shash_alg(calg);
	struct ahash_tfm *crt = &tfm->crt_ahash;
	struct crypto_shash **ctx = crypto_tfm_ctx(tfm);
	struct crypto_shash *shash;

	if (!crypto_mod_get(calg))
		return -EAGAIN;

	shash = __crypto_shash_cast(crypto_create_tfm(
		calg, &crypto_shash_type));
	if (IS_ERR(shash)) {
		crypto_mod_put(calg);
		return PTR_ERR(shash);
	}

	*ctx = shash;
	tfm->exit = crypto_exit_shash_ops_async;

	crt->init = shash_async_init;
	crt->update = shash_async_update;
	crt->final  = shash_async_final;
	crt->digest = shash_async_digest;
	crt->setkey = shash_async_setkey;

	crt->digestsize = alg->digestsize;
	crt->reqsize = sizeof(struct shash_desc) + crypto_shash_descsize(shash);

	return 0;
}

static int shash_compat_setkey(struct crypto_hash *tfm, const u8 *key,
			       unsigned int keylen)
{
	struct shash_desc *desc = crypto_hash_ctx(tfm);

	return crypto_shash_setkey(desc->tfm, key, keylen);
}

static int shash_compat_init(struct hash_desc *hdesc)
{
	struct shash_desc *desc = crypto_hash_ctx(hdesc->tfm);

	desc->flags = hdesc->flags;

	return crypto_shash_init(desc);
}

static int shash_compat_update(struct hash_desc *hdesc, struct scatterlist *sg,
			       unsigned int len)
{
	struct shash_desc *desc = crypto_hash_ctx(hdesc->tfm);
	struct crypto_hash_walk walk;
	int nbytes;

	for (nbytes = crypto_hash_walk_first_compat(hdesc, &walk, sg, len);
	     nbytes > 0; nbytes = crypto_hash_walk_done(&walk, nbytes))
		nbytes = crypto_shash_update(desc, walk.data, nbytes);

	return nbytes;
}

static int shash_compat_final(struct hash_desc *hdesc, u8 *out)
{
	return crypto_shash_final(crypto_hash_ctx(hdesc->tfm), out);
}

static int shash_compat_digest(struct hash_desc *hdesc, struct scatterlist *sg,
			       unsigned int nbytes, u8 *out)
{
	unsigned int offset = sg->offset;
	int err;

	if (nbytes < min(sg->length, ((unsigned int)(PAGE_SIZE)) - offset)) {
		struct shash_desc *desc = crypto_hash_ctx(hdesc->tfm);
		void *data;

		desc->flags = hdesc->flags;

		data = crypto_kmap(sg_page(sg), 0);
		err = crypto_shash_digest(desc, data + offset, nbytes, out);
		crypto_kunmap(data, 0);
		crypto_yield(desc->flags);
		goto out;
	}

	err = shash_compat_init(hdesc);
	if (err)
		goto out;

	err = shash_compat_update(hdesc, sg, nbytes);
	if (err)
		goto out;

	err = shash_compat_final(hdesc, out);

out:
	return err;
}

static void crypto_exit_shash_ops_compat(struct crypto_tfm *tfm)
{
	struct shash_desc *desc= crypto_tfm_ctx(tfm);

	crypto_free_shash(desc->tfm);
}

static int crypto_init_shash_ops_compat(struct crypto_tfm *tfm)
{
	struct hash_tfm *crt = &tfm->crt_hash;
	struct crypto_alg *calg = tfm->__crt_alg;
	struct shash_alg *alg = __crypto_shash_alg(calg);
	struct shash_desc *desc = crypto_tfm_ctx(tfm);
	struct crypto_shash *shash;

	if (!crypto_mod_get(calg))
		return -EAGAIN;

	shash = __crypto_shash_cast(crypto_create_tfm(
		calg, &crypto_shash_type));
	if (IS_ERR(shash)) {
		crypto_mod_put(calg);
		return PTR_ERR(shash);
	}

	desc->tfm = shash;
	tfm->exit = crypto_exit_shash_ops_compat;

	crt->init = shash_compat_init;
	crt->update = shash_compat_update;
	crt->final  = shash_compat_final;
	crt->digest = shash_compat_digest;
	crt->setkey = shash_compat_setkey;

	crt->digestsize = alg->digestsize;

	return 0;
}

static int crypto_init_shash_ops(struct crypto_tfm *tfm, u32 type, u32 mask)
{
	switch (mask & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_HASH_MASK:
		return crypto_init_shash_ops_compat(tfm);
	case CRYPTO_ALG_TYPE_AHASH_MASK:
		return crypto_init_shash_ops_async(tfm);
	}

	return -EINVAL;
}

static unsigned int crypto_shash_ctxsize(struct crypto_alg *alg, u32 type,
					 u32 mask)
{
	struct shash_alg *salg = __crypto_shash_alg(alg);

	switch (mask & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_HASH_MASK:
		return sizeof(struct shash_desc) + salg->descsize;
	case CRYPTO_ALG_TYPE_AHASH_MASK:
		return sizeof(struct crypto_shash *);
	}

	return 0;
}

static int crypto_shash_init_tfm(struct crypto_tfm *tfm,
				 const struct crypto_type *frontend)
{
	if (frontend->type != CRYPTO_ALG_TYPE_SHASH)
		return -EINVAL;
	return 0;
}

static unsigned int crypto_shash_extsize(struct crypto_alg *alg,
					 const struct crypto_type *frontend)
{
	return alg->cra_ctxsize;
}

static void crypto_shash_show(struct seq_file *m, struct crypto_alg *alg)
	__attribute__ ((unused));
static void crypto_shash_show(struct seq_file *m, struct crypto_alg *alg)
{
	struct shash_alg *salg = __crypto_shash_alg(alg);

	seq_printf(m, "type         : shash\n");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "digestsize   : %u\n", salg->digestsize);
	seq_printf(m, "descsize     : %u\n", salg->descsize);
}

static const struct crypto_type crypto_shash_type = {
	.ctxsize = crypto_shash_ctxsize,
	.extsize = crypto_shash_extsize,
	.init = crypto_init_shash_ops,
	.init_tfm = crypto_shash_init_tfm,
#ifdef CONFIG_PROC_FS
	.show = crypto_shash_show,
#endif
	.maskclear = ~CRYPTO_ALG_TYPE_MASK,
	.maskset = CRYPTO_ALG_TYPE_MASK,
	.type = CRYPTO_ALG_TYPE_SHASH,
	.tfmsize = offsetof(struct crypto_shash, base),
};

struct crypto_shash *crypto_alloc_shash(const char *alg_name, u32 type,
					u32 mask)
{
	return __crypto_shash_cast(
		crypto_alloc_tfm(alg_name, &crypto_shash_type, type, mask));
}
EXPORT_SYMBOL_GPL(crypto_alloc_shash);

int crypto_register_shash(struct shash_alg *alg)
{
	struct crypto_alg *base = &alg->base;

	if (alg->digestsize > PAGE_SIZE / 8 ||
	    alg->descsize > PAGE_SIZE / 8)
		return -EINVAL;

	base->cra_type = &crypto_shash_type;
	base->cra_flags &= ~CRYPTO_ALG_TYPE_MASK;
	base->cra_flags |= CRYPTO_ALG_TYPE_SHASH;

	return crypto_register_alg(base);
}
EXPORT_SYMBOL_GPL(crypto_register_shash);

int crypto_unregister_shash(struct shash_alg *alg)
{
	return crypto_unregister_alg(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_unregister_shash);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synchronous cryptographic hash type");
