/*
 * Cryptographic API.
 *
 * HMAC: Keyed-Hashing for Message Authentication (RFC2104).
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
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
#include <linux/crypto.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <asm/scatterlist.h>
#include "internal.h"

static void hash_key(struct crypto_tfm *tfm, u8 *key, unsigned int keylen)
{
	struct scatterlist tmp;
	
	tmp.page = virt_to_page(key);
	tmp.offset = offset_in_page(key);
	tmp.length = keylen;
	crypto_digest_digest(tfm, &tmp, 1, key);
		
}

int crypto_alloc_hmac_block(struct crypto_tfm *tfm)
{
	int ret = 0;

	BUG_ON(!crypto_tfm_alg_blocksize(tfm));
	
	tfm->crt_digest.dit_hmac_block = kmalloc(crypto_tfm_alg_blocksize(tfm),
	                                         GFP_KERNEL);
	if (tfm->crt_digest.dit_hmac_block == NULL)
		ret = -ENOMEM;

	return ret;
		
}

void crypto_free_hmac_block(struct crypto_tfm *tfm)
{
	if (tfm->crt_digest.dit_hmac_block)
		kfree(tfm->crt_digest.dit_hmac_block);
}

void crypto_hmac_init(struct crypto_tfm *tfm, u8 *key, unsigned int *keylen)
{
	unsigned int i;
	struct scatterlist tmp;
	char *ipad = tfm->crt_digest.dit_hmac_block;
	
	if (*keylen > crypto_tfm_alg_blocksize(tfm)) {
		hash_key(tfm, key, *keylen);
		*keylen = crypto_tfm_alg_digestsize(tfm);
	}

	memset(ipad, 0, crypto_tfm_alg_blocksize(tfm));
	memcpy(ipad, key, *keylen);

	for (i = 0; i < crypto_tfm_alg_blocksize(tfm); i++)
		ipad[i] ^= 0x36;

	tmp.page = virt_to_page(ipad);
	tmp.offset = offset_in_page(ipad);
	tmp.length = crypto_tfm_alg_blocksize(tfm);
	
	crypto_digest_init(tfm);
	crypto_digest_update(tfm, &tmp, 1);
}

void crypto_hmac_update(struct crypto_tfm *tfm,
                        struct scatterlist *sg, unsigned int nsg)
{
	crypto_digest_update(tfm, sg, nsg);
}

void crypto_hmac_final(struct crypto_tfm *tfm, u8 *key,
                       unsigned int *keylen, u8 *out)
{
	unsigned int i;
	struct scatterlist tmp;
	char *opad = tfm->crt_digest.dit_hmac_block;
	
	if (*keylen > crypto_tfm_alg_blocksize(tfm)) {
		hash_key(tfm, key, *keylen);
		*keylen = crypto_tfm_alg_digestsize(tfm);
	}

	crypto_digest_final(tfm, out);

	memset(opad, 0, crypto_tfm_alg_blocksize(tfm));
	memcpy(opad, key, *keylen);
		
	for (i = 0; i < crypto_tfm_alg_blocksize(tfm); i++)
		opad[i] ^= 0x5c;

	tmp.page = virt_to_page(opad);
	tmp.offset = offset_in_page(opad);
	tmp.length = crypto_tfm_alg_blocksize(tfm);

	crypto_digest_init(tfm);
	crypto_digest_update(tfm, &tmp, 1);
	
	tmp.page = virt_to_page(out);
	tmp.offset = offset_in_page(out);
	tmp.length = crypto_tfm_alg_digestsize(tfm);
	
	crypto_digest_update(tfm, &tmp, 1);
	crypto_digest_final(tfm, out);
}

void crypto_hmac(struct crypto_tfm *tfm, u8 *key, unsigned int *keylen,
                 struct scatterlist *sg, unsigned int nsg, u8 *out)
{
	crypto_hmac_init(tfm, key, keylen);
	crypto_hmac_update(tfm, sg, nsg);
	crypto_hmac_final(tfm, key, keylen, out);
}

EXPORT_SYMBOL_GPL(crypto_hmac_init);
EXPORT_SYMBOL_GPL(crypto_hmac_update);
EXPORT_SYMBOL_GPL(crypto_hmac_final);
EXPORT_SYMBOL_GPL(crypto_hmac);

