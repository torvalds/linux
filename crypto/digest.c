/*
 * Cryptographic API.
 *
 * Digest operations.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/hardirq.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>

#include "internal.h"
#include "scatterwalk.h"

static int init(struct hash_desc *desc)
{
	struct crypto_tfm *tfm = crypto_hash_tfm(desc->tfm);

	tfm->__crt_alg->cra_digest.dia_init(tfm);
	return 0;
}

static int update2(struct hash_desc *desc,
		   struct scatterlist *sg, unsigned int nbytes)
{
	struct crypto_tfm *tfm = crypto_hash_tfm(desc->tfm);
	unsigned int alignmask = crypto_tfm_alg_alignmask(tfm);

	if (!nbytes)
		return 0;

	for (;;) {
		struct page *pg = sg->page;
		unsigned int offset = sg->offset;
		unsigned int l = sg->length;

		if (unlikely(l > nbytes))
			l = nbytes;
		nbytes -= l;

		do {
			unsigned int bytes_from_page = min(l, ((unsigned int)
							   (PAGE_SIZE)) - 
							   offset);
			char *src = crypto_kmap(pg, 0);
			char *p = src + offset;

			if (unlikely(offset & alignmask)) {
				unsigned int bytes =
					alignmask + 1 - (offset & alignmask);
				bytes = min(bytes, bytes_from_page);
				tfm->__crt_alg->cra_digest.dia_update(tfm, p,
								      bytes);
				p += bytes;
				bytes_from_page -= bytes;
				l -= bytes;
			}
			tfm->__crt_alg->cra_digest.dia_update(tfm, p,
							      bytes_from_page);
			crypto_kunmap(src, 0);
			crypto_yield(desc->flags);
			offset = 0;
			pg++;
			l -= bytes_from_page;
		} while (l > 0);

		if (!nbytes)
			break;
		sg = scatterwalk_sg_next(sg);
	}

	return 0;
}

static int update(struct hash_desc *desc,
		  struct scatterlist *sg, unsigned int nbytes)
{
	if (WARN_ON_ONCE(in_irq()))
		return -EDEADLK;
	return update2(desc, sg, nbytes);
}

static int final(struct hash_desc *desc, u8 *out)
{
	struct crypto_tfm *tfm = crypto_hash_tfm(desc->tfm);
	unsigned long alignmask = crypto_tfm_alg_alignmask(tfm);
	struct digest_alg *digest = &tfm->__crt_alg->cra_digest;

	if (unlikely((unsigned long)out & alignmask)) {
		unsigned long align = alignmask + 1;
		unsigned long addr = (unsigned long)crypto_tfm_ctx(tfm);
		u8 *dst = (u8 *)ALIGN(addr, align) +
			  ALIGN(tfm->__crt_alg->cra_ctxsize, align);

		digest->dia_final(tfm, dst);
		memcpy(out, dst, digest->dia_digestsize);
	} else
		digest->dia_final(tfm, out);

	return 0;
}

static int nosetkey(struct crypto_hash *tfm, const u8 *key, unsigned int keylen)
{
	crypto_hash_clear_flags(tfm, CRYPTO_TFM_RES_MASK);
	return -ENOSYS;
}

static int setkey(struct crypto_hash *hash, const u8 *key, unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_hash_tfm(hash);

	crypto_hash_clear_flags(hash, CRYPTO_TFM_RES_MASK);
	return tfm->__crt_alg->cra_digest.dia_setkey(tfm, key, keylen);
}

static int digest(struct hash_desc *desc,
		  struct scatterlist *sg, unsigned int nbytes, u8 *out)
{
	if (WARN_ON_ONCE(in_irq()))
		return -EDEADLK;

	init(desc);
	update2(desc, sg, nbytes);
	return final(desc, out);
}

int crypto_init_digest_ops(struct crypto_tfm *tfm)
{
	struct hash_tfm *ops = &tfm->crt_hash;
	struct digest_alg *dalg = &tfm->__crt_alg->cra_digest;

	if (dalg->dia_digestsize > crypto_tfm_alg_blocksize(tfm))
		return -EINVAL;
	
	ops->init	= init;
	ops->update	= update;
	ops->final	= final;
	ops->digest	= digest;
	ops->setkey	= dalg->dia_setkey ? setkey : nosetkey;
	ops->digestsize	= dalg->dia_digestsize;
	
	return 0;
}

void crypto_exit_digest_ops(struct crypto_tfm *tfm)
{
}
