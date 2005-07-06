/*
 * Cryptographic API.
 *
 * Cipher operations.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2005 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/crypto.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/scatterlist.h>
#include "internal.h"
#include "scatterwalk.h"

struct cipher_desc {
	struct crypto_tfm *tfm;
	void (*crfn)(void *ctx, u8 *dst, const u8 *src);
	unsigned int (*prfn)(const struct cipher_desc *desc, u8 *dst,
			     const u8 *src, unsigned int nbytes);
	void *info;
};

static inline void xor_64(u8 *a, const u8 *b)
{
	((u32 *)a)[0] ^= ((u32 *)b)[0];
	((u32 *)a)[1] ^= ((u32 *)b)[1];
}

static inline void xor_128(u8 *a, const u8 *b)
{
	((u32 *)a)[0] ^= ((u32 *)b)[0];
	((u32 *)a)[1] ^= ((u32 *)b)[1];
	((u32 *)a)[2] ^= ((u32 *)b)[2];
	((u32 *)a)[3] ^= ((u32 *)b)[3];
}

static unsigned int crypt_slow(const struct cipher_desc *desc,
			       struct scatter_walk *in,
			       struct scatter_walk *out, unsigned int bsize)
{
	u8 src[bsize];
	u8 dst[bsize];
	unsigned int n;

	n = scatterwalk_copychunks(src, in, bsize, 0);
	scatterwalk_advance(in, n);

	desc->prfn(desc, dst, src, bsize);

	n = scatterwalk_copychunks(dst, out, bsize, 1);
	scatterwalk_advance(out, n);

	return bsize;
}

static inline unsigned int crypt_fast(const struct cipher_desc *desc,
				      struct scatter_walk *in,
				      struct scatter_walk *out,
				      unsigned int nbytes)
{
	u8 *src, *dst;

	src = in->data;
	dst = scatterwalk_samebuf(in, out) ? src : out->data;

	nbytes = desc->prfn(desc, dst, src, nbytes);

	scatterwalk_advance(in, nbytes);
	scatterwalk_advance(out, nbytes);

	return nbytes;
}

/* 
 * Generic encrypt/decrypt wrapper for ciphers, handles operations across
 * multiple page boundaries by using temporary blocks.  In user context,
 * the kernel is given a chance to schedule us once per page.
 */
static int crypt(const struct cipher_desc *desc,
		 struct scatterlist *dst,
		 struct scatterlist *src,
		 unsigned int nbytes)
{
	struct scatter_walk walk_in, walk_out;
	struct crypto_tfm *tfm = desc->tfm;
	const unsigned int bsize = crypto_tfm_alg_blocksize(tfm);

	if (!nbytes)
		return 0;

	if (nbytes % bsize) {
		tfm->crt_flags |= CRYPTO_TFM_RES_BAD_BLOCK_LEN;
		return -EINVAL;
	}

	scatterwalk_start(&walk_in, src);
	scatterwalk_start(&walk_out, dst);

	for(;;) {
		unsigned int n;

		scatterwalk_map(&walk_in, 0);
		scatterwalk_map(&walk_out, 1);

		n = scatterwalk_clamp(&walk_in, nbytes);
		n = scatterwalk_clamp(&walk_out, n);

		if (likely(n >= bsize))
			n = crypt_fast(desc, &walk_in, &walk_out, n);
		else
			n = crypt_slow(desc, &walk_in, &walk_out, bsize);

		nbytes -= n;

		scatterwalk_done(&walk_in, 0, nbytes);
		scatterwalk_done(&walk_out, 1, nbytes);

		if (!nbytes)
			return 0;

		crypto_yield(tfm);
	}
}

static unsigned int cbc_process_encrypt(const struct cipher_desc *desc,
					u8 *dst, const u8 *src,
					unsigned int nbytes)
{
	struct crypto_tfm *tfm = desc->tfm;
	void (*xor)(u8 *, const u8 *) = tfm->crt_u.cipher.cit_xor_block;
	int bsize = crypto_tfm_alg_blocksize(tfm);

	void (*fn)(void *, u8 *, const u8 *) = desc->crfn;
	u8 *iv = desc->info;
	unsigned int done = 0;

	do {
		xor(iv, src);
		fn(crypto_tfm_ctx(tfm), dst, iv);
		memcpy(iv, dst, bsize);

		src += bsize;
		dst += bsize;
	} while ((done += bsize) < nbytes);

	return done;
}

static unsigned int cbc_process_decrypt(const struct cipher_desc *desc,
					u8 *dst, const u8 *src,
					unsigned int nbytes)
{
	struct crypto_tfm *tfm = desc->tfm;
	void (*xor)(u8 *, const u8 *) = tfm->crt_u.cipher.cit_xor_block;
	int bsize = crypto_tfm_alg_blocksize(tfm);

	u8 stack[src == dst ? bsize : 0];
	u8 *buf = stack;
	u8 **dst_p = src == dst ? &buf : &dst;

	void (*fn)(void *, u8 *, const u8 *) = desc->crfn;
	u8 *iv = desc->info;
	unsigned int done = 0;

	do {
		u8 *tmp_dst = *dst_p;

		fn(crypto_tfm_ctx(tfm), tmp_dst, src);
		xor(tmp_dst, iv);
		memcpy(iv, src, bsize);
		if (tmp_dst != dst)
			memcpy(dst, tmp_dst, bsize);

		src += bsize;
		dst += bsize;
	} while ((done += bsize) < nbytes);

	return done;
}

static unsigned int ecb_process(const struct cipher_desc *desc, u8 *dst,
				const u8 *src, unsigned int nbytes)
{
	struct crypto_tfm *tfm = desc->tfm;
	int bsize = crypto_tfm_alg_blocksize(tfm);
	void (*fn)(void *, u8 *, const u8 *) = desc->crfn;
	unsigned int done = 0;

	do {
		fn(crypto_tfm_ctx(tfm), dst, src);

		src += bsize;
		dst += bsize;
	} while ((done += bsize) < nbytes);

	return done;
}

static int setkey(struct crypto_tfm *tfm, const u8 *key, unsigned int keylen)
{
	struct cipher_alg *cia = &tfm->__crt_alg->cra_cipher;
	
	if (keylen < cia->cia_min_keysize || keylen > cia->cia_max_keysize) {
		tfm->crt_flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	} else
		return cia->cia_setkey(crypto_tfm_ctx(tfm), key, keylen,
		                       &tfm->crt_flags);
}

static int ecb_encrypt(struct crypto_tfm *tfm,
		       struct scatterlist *dst,
                       struct scatterlist *src, unsigned int nbytes)
{
	struct cipher_desc desc;

	desc.tfm = tfm;
	desc.crfn = tfm->__crt_alg->cra_cipher.cia_encrypt;
	desc.prfn = ecb_process;

	return crypt(&desc, dst, src, nbytes);
}

static int ecb_decrypt(struct crypto_tfm *tfm,
                       struct scatterlist *dst,
                       struct scatterlist *src,
		       unsigned int nbytes)
{
	struct cipher_desc desc;

	desc.tfm = tfm;
	desc.crfn = tfm->__crt_alg->cra_cipher.cia_decrypt;
	desc.prfn = ecb_process;

	return crypt(&desc, dst, src, nbytes);
}

static int cbc_encrypt(struct crypto_tfm *tfm,
                       struct scatterlist *dst,
                       struct scatterlist *src,
		       unsigned int nbytes)
{
	struct cipher_desc desc;

	desc.tfm = tfm;
	desc.crfn = tfm->__crt_alg->cra_cipher.cia_encrypt;
	desc.prfn = cbc_process_encrypt;
	desc.info = tfm->crt_cipher.cit_iv;

	return crypt(&desc, dst, src, nbytes);
}

static int cbc_encrypt_iv(struct crypto_tfm *tfm,
                          struct scatterlist *dst,
                          struct scatterlist *src,
                          unsigned int nbytes, u8 *iv)
{
	struct cipher_desc desc;

	desc.tfm = tfm;
	desc.crfn = tfm->__crt_alg->cra_cipher.cia_encrypt;
	desc.prfn = cbc_process_encrypt;
	desc.info = iv;

	return crypt(&desc, dst, src, nbytes);
}

static int cbc_decrypt(struct crypto_tfm *tfm,
                       struct scatterlist *dst,
                       struct scatterlist *src,
		       unsigned int nbytes)
{
	struct cipher_desc desc;

	desc.tfm = tfm;
	desc.crfn = tfm->__crt_alg->cra_cipher.cia_decrypt;
	desc.prfn = cbc_process_decrypt;
	desc.info = tfm->crt_cipher.cit_iv;

	return crypt(&desc, dst, src, nbytes);
}

static int cbc_decrypt_iv(struct crypto_tfm *tfm,
                          struct scatterlist *dst,
                          struct scatterlist *src,
                          unsigned int nbytes, u8 *iv)
{
	struct cipher_desc desc;

	desc.tfm = tfm;
	desc.crfn = tfm->__crt_alg->cra_cipher.cia_decrypt;
	desc.prfn = cbc_process_decrypt;
	desc.info = iv;

	return crypt(&desc, dst, src, nbytes);
}

static int nocrypt(struct crypto_tfm *tfm,
                   struct scatterlist *dst,
                   struct scatterlist *src,
		   unsigned int nbytes)
{
	return -ENOSYS;
}

static int nocrypt_iv(struct crypto_tfm *tfm,
                      struct scatterlist *dst,
                      struct scatterlist *src,
                      unsigned int nbytes, u8 *iv)
{
	return -ENOSYS;
}

int crypto_init_cipher_flags(struct crypto_tfm *tfm, u32 flags)
{
	u32 mode = flags & CRYPTO_TFM_MODE_MASK;
	
	tfm->crt_cipher.cit_mode = mode ? mode : CRYPTO_TFM_MODE_ECB;
	if (flags & CRYPTO_TFM_REQ_WEAK_KEY)
		tfm->crt_flags = CRYPTO_TFM_REQ_WEAK_KEY;
	
	return 0;
}

int crypto_init_cipher_ops(struct crypto_tfm *tfm)
{
	int ret = 0;
	struct cipher_tfm *ops = &tfm->crt_cipher;

	ops->cit_setkey = setkey;

	switch (tfm->crt_cipher.cit_mode) {
	case CRYPTO_TFM_MODE_ECB:
		ops->cit_encrypt = ecb_encrypt;
		ops->cit_decrypt = ecb_decrypt;
		break;
		
	case CRYPTO_TFM_MODE_CBC:
		ops->cit_encrypt = cbc_encrypt;
		ops->cit_decrypt = cbc_decrypt;
		ops->cit_encrypt_iv = cbc_encrypt_iv;
		ops->cit_decrypt_iv = cbc_decrypt_iv;
		break;
		
	case CRYPTO_TFM_MODE_CFB:
		ops->cit_encrypt = nocrypt;
		ops->cit_decrypt = nocrypt;
		ops->cit_encrypt_iv = nocrypt_iv;
		ops->cit_decrypt_iv = nocrypt_iv;
		break;
	
	case CRYPTO_TFM_MODE_CTR:
		ops->cit_encrypt = nocrypt;
		ops->cit_decrypt = nocrypt;
		ops->cit_encrypt_iv = nocrypt_iv;
		ops->cit_decrypt_iv = nocrypt_iv;
		break;

	default:
		BUG();
	}
	
	if (ops->cit_mode == CRYPTO_TFM_MODE_CBC) {
	    	
	    	switch (crypto_tfm_alg_blocksize(tfm)) {
	    	case 8:
	    		ops->cit_xor_block = xor_64;
	    		break;
	    		
	    	case 16:
	    		ops->cit_xor_block = xor_128;
	    		break;
	    		
	    	default:
	    		printk(KERN_WARNING "%s: block size %u not supported\n",
	    		       crypto_tfm_alg_name(tfm),
	    		       crypto_tfm_alg_blocksize(tfm));
	    		ret = -EINVAL;
	    		goto out;
	    	}
	    	
		ops->cit_ivsize = crypto_tfm_alg_blocksize(tfm);
	    	ops->cit_iv = kmalloc(ops->cit_ivsize, GFP_KERNEL);
		if (ops->cit_iv == NULL)
			ret = -ENOMEM;
	}

out:	
	return ret;
}

void crypto_exit_cipher_ops(struct crypto_tfm *tfm)
{
	kfree(tfm->crt_cipher.cit_iv);
}
