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
#include <linux/crypto.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/highmem.h>
#include <asm/scatterlist.h>
#include "internal.h"

static void init(struct crypto_tfm *tfm)
{
	tfm->__crt_alg->cra_digest.dia_init(tfm);
}

static void update(struct crypto_tfm *tfm,
                   struct scatterlist *sg, unsigned int nsg)
{
	unsigned int i;
	unsigned int alignmask = crypto_tfm_alg_alignmask(tfm);

	for (i = 0; i < nsg; i++) {

		struct page *pg = sg[i].page;
		unsigned int offset = sg[i].offset;
		unsigned int l = sg[i].length;

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
			crypto_yield(tfm);
			offset = 0;
			pg++;
			l -= bytes_from_page;
		} while (l > 0);
	}
}

static void final(struct crypto_tfm *tfm, u8 *out)
{
	unsigned long alignmask = crypto_tfm_alg_alignmask(tfm);
	if (unlikely((unsigned long)out & alignmask)) {
		unsigned int size = crypto_tfm_alg_digestsize(tfm);
		u8 buffer[size + alignmask];
		u8 *dst = (u8 *)ALIGN((unsigned long)buffer, alignmask + 1);
		tfm->__crt_alg->cra_digest.dia_final(tfm, dst);
		memcpy(out, dst, size);
	} else
		tfm->__crt_alg->cra_digest.dia_final(tfm, out);
}

static int nosetkey(struct crypto_tfm *tfm, const u8 *key, unsigned int keylen)
{
	tfm->crt_flags &= ~CRYPTO_TFM_RES_MASK;
	return -ENOSYS;
}

static int setkey(struct crypto_tfm *tfm, const u8 *key, unsigned int keylen)
{
	tfm->crt_flags &= ~CRYPTO_TFM_RES_MASK;
	return tfm->__crt_alg->cra_digest.dia_setkey(tfm, key, keylen);
}

static void digest(struct crypto_tfm *tfm,
                   struct scatterlist *sg, unsigned int nsg, u8 *out)
{
	init(tfm);
	update(tfm, sg, nsg);
	final(tfm, out);
}

int crypto_init_digest_flags(struct crypto_tfm *tfm, u32 flags)
{
	return flags ? -EINVAL : 0;
}

int crypto_init_digest_ops(struct crypto_tfm *tfm)
{
	struct digest_tfm *ops = &tfm->crt_digest;
	struct digest_alg *dalg = &tfm->__crt_alg->cra_digest;
	
	ops->dit_init	= init;
	ops->dit_update	= update;
	ops->dit_final	= final;
	ops->dit_digest	= digest;
	ops->dit_setkey	= dalg->dia_setkey ? setkey : nosetkey;
	
	return crypto_alloc_hmac_block(tfm);
}

void crypto_exit_digest_ops(struct crypto_tfm *tfm)
{
	crypto_free_hmac_block(tfm);
}
