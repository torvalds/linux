/*
 * Asynchronous Cryptographic Hash operations.
 *
 * This is the asynchronous version of hash.c with notification of
 * completion via a callback.
 *
 * Copyright (c) 2008 Loc Ho <lho@amcc.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/algapi.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/seq_file.h>

#include "internal.h"

static int ahash_setkey_unaligned(struct crypto_ahash *tfm, const u8 *key,
				unsigned int keylen)
{
	struct ahash_alg *ahash = crypto_ahash_alg(tfm);
	unsigned long alignmask = crypto_ahash_alignmask(tfm);
	int ret;
	u8 *buffer, *alignbuffer;
	unsigned long absize;

	absize = keylen + alignmask;
	buffer = kmalloc(absize, GFP_ATOMIC);
	if (!buffer)
		return -ENOMEM;

	alignbuffer = (u8 *)ALIGN((unsigned long)buffer, alignmask + 1);
	memcpy(alignbuffer, key, keylen);
	ret = ahash->setkey(tfm, alignbuffer, keylen);
	memset(alignbuffer, 0, keylen);
	kfree(buffer);
	return ret;
}

static int ahash_setkey(struct crypto_ahash *tfm, const u8 *key,
			unsigned int keylen)
{
	struct ahash_alg *ahash = crypto_ahash_alg(tfm);
	unsigned long alignmask = crypto_ahash_alignmask(tfm);

	if ((unsigned long)key & alignmask)
		return ahash_setkey_unaligned(tfm, key, keylen);

	return ahash->setkey(tfm, key, keylen);
}

static unsigned int crypto_ahash_ctxsize(struct crypto_alg *alg, u32 type,
					u32 mask)
{
	return alg->cra_ctxsize;
}

static int crypto_init_ahash_ops(struct crypto_tfm *tfm, u32 type, u32 mask)
{
	struct ahash_alg *alg = &tfm->__crt_alg->cra_ahash;
	struct ahash_tfm *crt   = &tfm->crt_ahash;

	if (alg->digestsize > crypto_tfm_alg_blocksize(tfm))
		return -EINVAL;

	crt->init = alg->init;
	crt->update = alg->update;
	crt->final  = alg->final;
	crt->digest = alg->digest;
	crt->setkey = ahash_setkey;
	crt->base   = __crypto_ahash_cast(tfm);
	crt->digestsize = alg->digestsize;

	return 0;
}

static void crypto_ahash_show(struct seq_file *m, struct crypto_alg *alg)
	__attribute__ ((unused));
static void crypto_ahash_show(struct seq_file *m, struct crypto_alg *alg)
{
	seq_printf(m, "type         : ahash\n");
	seq_printf(m, "async        : %s\n", alg->cra_flags & CRYPTO_ALG_ASYNC ?
					     "yes" : "no");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "digestsize   : %u\n", alg->cra_hash.digestsize);
}

const struct crypto_type crypto_ahash_type = {
	.ctxsize = crypto_ahash_ctxsize,
	.init = crypto_init_ahash_ops,
#ifdef CONFIG_PROC_FS
	.show = crypto_ahash_show,
#endif
};
EXPORT_SYMBOL_GPL(crypto_ahash_type);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Asynchronous cryptographic hash type");
