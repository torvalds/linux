// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API.
 *
 * Single-block cipher operations.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2005 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/algapi.h>
#include <linux/kernel.h>
#include <linux/crypto.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "internal.h"

static int setkey_unaligned(struct crypto_cipher *tfm, const u8 *key,
			    unsigned int keylen)
{
	struct cipher_alg *cia = crypto_cipher_alg(tfm);
	unsigned long alignmask = crypto_cipher_alignmask(tfm);
	int ret;
	u8 *buffer, *alignbuffer;
	unsigned long absize;

	absize = keylen + alignmask;
	buffer = kmalloc(absize, GFP_ATOMIC);
	if (!buffer)
		return -ENOMEM;

	alignbuffer = (u8 *)ALIGN((unsigned long)buffer, alignmask + 1);
	memcpy(alignbuffer, key, keylen);
	ret = cia->cia_setkey(crypto_cipher_tfm(tfm), alignbuffer, keylen);
	memset(alignbuffer, 0, keylen);
	kfree(buffer);
	return ret;

}

int crypto_cipher_setkey(struct crypto_cipher *tfm,
			 const u8 *key, unsigned int keylen)
{
	struct cipher_alg *cia = crypto_cipher_alg(tfm);
	unsigned long alignmask = crypto_cipher_alignmask(tfm);

	if (keylen < cia->cia_min_keysize || keylen > cia->cia_max_keysize)
		return -EINVAL;

	if ((unsigned long)key & alignmask)
		return setkey_unaligned(tfm, key, keylen);

	return cia->cia_setkey(crypto_cipher_tfm(tfm), key, keylen);
}
EXPORT_SYMBOL_GPL(crypto_cipher_setkey);

static inline void cipher_crypt_one(struct crypto_cipher *tfm,
				    u8 *dst, const u8 *src, bool enc)
{
	unsigned long alignmask = crypto_cipher_alignmask(tfm);
	struct cipher_alg *cia = crypto_cipher_alg(tfm);
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *) =
		enc ? cia->cia_encrypt : cia->cia_decrypt;

	if (unlikely(((unsigned long)dst | (unsigned long)src) & alignmask)) {
		unsigned int bs = crypto_cipher_blocksize(tfm);
		u8 buffer[MAX_CIPHER_BLOCKSIZE + MAX_CIPHER_ALIGNMASK];
		u8 *tmp = (u8 *)ALIGN((unsigned long)buffer, alignmask + 1);

		memcpy(tmp, src, bs);
		fn(crypto_cipher_tfm(tfm), tmp, tmp);
		memcpy(dst, tmp, bs);
	} else {
		fn(crypto_cipher_tfm(tfm), dst, src);
	}
}

void crypto_cipher_encrypt_one(struct crypto_cipher *tfm,
			       u8 *dst, const u8 *src)
{
	cipher_crypt_one(tfm, dst, src, true);
}
EXPORT_SYMBOL_GPL(crypto_cipher_encrypt_one);

void crypto_cipher_decrypt_one(struct crypto_cipher *tfm,
			       u8 *dst, const u8 *src)
{
	cipher_crypt_one(tfm, dst, src, false);
}
EXPORT_SYMBOL_GPL(crypto_cipher_decrypt_one);
