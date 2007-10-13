/*
 * AEAD: Authenticated Encryption with Associated Data
 * 
 * This file provides API support for AEAD algorithms.
 *
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#include <crypto/algapi.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/seq_file.h>

static int setkey_unaligned(struct crypto_aead *tfm, const u8 *key,
			    unsigned int keylen)
{
	struct aead_alg *aead = crypto_aead_alg(tfm);
	unsigned long alignmask = crypto_aead_alignmask(tfm);
	int ret;
	u8 *buffer, *alignbuffer;
	unsigned long absize;

	absize = keylen + alignmask;
	buffer = kmalloc(absize, GFP_ATOMIC);
	if (!buffer)
		return -ENOMEM;

	alignbuffer = (u8 *)ALIGN((unsigned long)buffer, alignmask + 1);
	memcpy(alignbuffer, key, keylen);
	ret = aead->setkey(tfm, alignbuffer, keylen);
	memset(alignbuffer, 0, keylen);
	kfree(buffer);
	return ret;
}

static int setkey(struct crypto_aead *tfm, const u8 *key, unsigned int keylen)
{
	struct aead_alg *aead = crypto_aead_alg(tfm);
	unsigned long alignmask = crypto_aead_alignmask(tfm);

	if ((unsigned long)key & alignmask)
		return setkey_unaligned(tfm, key, keylen);

	return aead->setkey(tfm, key, keylen);
}

static unsigned int crypto_aead_ctxsize(struct crypto_alg *alg, u32 type,
					u32 mask)
{
	return alg->cra_ctxsize;
}

static int crypto_init_aead_ops(struct crypto_tfm *tfm, u32 type, u32 mask)
{
	struct aead_alg *alg = &tfm->__crt_alg->cra_aead;
	struct aead_tfm *crt = &tfm->crt_aead;

	if (max(alg->authsize, alg->ivsize) > PAGE_SIZE / 8)
		return -EINVAL;

	crt->setkey = setkey;
	crt->encrypt = alg->encrypt;
	crt->decrypt = alg->decrypt;
	crt->ivsize = alg->ivsize;
	crt->authsize = alg->authsize;

	return 0;
}

static void crypto_aead_show(struct seq_file *m, struct crypto_alg *alg)
	__attribute__ ((unused));
static void crypto_aead_show(struct seq_file *m, struct crypto_alg *alg)
{
	struct aead_alg *aead = &alg->cra_aead;

	seq_printf(m, "type         : aead\n");
	seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
	seq_printf(m, "ivsize       : %u\n", aead->ivsize);
	seq_printf(m, "authsize     : %u\n", aead->authsize);
}

const struct crypto_type crypto_aead_type = {
	.ctxsize = crypto_aead_ctxsize,
	.init = crypto_init_aead_ops,
#ifdef CONFIG_PROC_FS
	.show = crypto_aead_show,
#endif
};
EXPORT_SYMBOL_GPL(crypto_aead_type);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Authenticated Encryption with Associated Data (AEAD)");
