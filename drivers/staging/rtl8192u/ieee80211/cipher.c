/*
 * Cryptographic API.
 *
 * Cipher operations.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#include <linux/kernel.h>
//#include <linux/crypto.h>
#include "rtl_crypto.h"
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/scatterlist.h>
#include "internal.h"
#include "scatterwalk.h"

typedef void (cryptfn_t)(void *, u8 *, const u8 *);
typedef void (procfn_t)(struct crypto_tfm *, u8 *,
			u8*, cryptfn_t, int enc, void *, int);

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


/*
 * Generic encrypt/decrypt wrapper for ciphers, handles operations across
 * multiple page boundaries by using temporary blocks.  In user context,
 * the kernel is given a chance to schedule us once per block.
 */
static int crypt(struct crypto_tfm *tfm,
		 struct scatterlist *dst,
		 struct scatterlist *src,
		 unsigned int nbytes, cryptfn_t crfn,
		 procfn_t prfn, int enc, void *info)
{
	struct scatter_walk walk_in, walk_out;
	const unsigned int bsize = crypto_tfm_alg_blocksize(tfm);
	u8 tmp_src[bsize];
	u8 tmp_dst[bsize];

	if (!nbytes)
		return 0;

	if (nbytes % bsize) {
		tfm->crt_flags |= CRYPTO_TFM_RES_BAD_BLOCK_LEN;
		return -EINVAL;
	}

	scatterwalk_start(&walk_in, src);
	scatterwalk_start(&walk_out, dst);

	for(;;) {
		u8 *src_p, *dst_p;
		int in_place;

		scatterwalk_map(&walk_in, 0);
		scatterwalk_map(&walk_out, 1);
		src_p = scatterwalk_whichbuf(&walk_in, bsize, tmp_src);
		dst_p = scatterwalk_whichbuf(&walk_out, bsize, tmp_dst);
		in_place = scatterwalk_samebuf(&walk_in, &walk_out,
					       src_p, dst_p);

		nbytes -= bsize;

		scatterwalk_copychunks(src_p, &walk_in, bsize, 0);

		prfn(tfm, dst_p, src_p, crfn, enc, info, in_place);

		scatterwalk_done(&walk_in, 0, nbytes);

		scatterwalk_copychunks(dst_p, &walk_out, bsize, 1);
		scatterwalk_done(&walk_out, 1, nbytes);

		if (!nbytes)
			return 0;

		crypto_yield(tfm);
	}
}

static void cbc_process(struct crypto_tfm *tfm, u8 *dst, u8 *src,
			cryptfn_t fn, int enc, void *info, int in_place)
{
	u8 *iv = info;

	/* Null encryption */
	if (!iv)
		return;

	if (enc) {
		tfm->crt_u.cipher.cit_xor_block(iv, src);
		fn(crypto_tfm_ctx(tfm), dst, iv);
		memcpy(iv, dst, crypto_tfm_alg_blocksize(tfm));
	} else {
		u8 stack[in_place ? crypto_tfm_alg_blocksize(tfm) : 0];
		u8 *buf = in_place ? stack : dst;

		fn(crypto_tfm_ctx(tfm), buf, src);
		tfm->crt_u.cipher.cit_xor_block(buf, iv);
		memcpy(iv, src, crypto_tfm_alg_blocksize(tfm));
		if (buf != dst)
			memcpy(dst, buf, crypto_tfm_alg_blocksize(tfm));
	}
}

static void ecb_process(struct crypto_tfm *tfm, u8 *dst, u8 *src,
			cryptfn_t fn, int enc, void *info, int in_place)
{
	fn(crypto_tfm_ctx(tfm), dst, src);
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
	return crypt(tfm, dst, src, nbytes,
		     tfm->__crt_alg->cra_cipher.cia_encrypt,
		     ecb_process, 1, NULL);
}

static int ecb_decrypt(struct crypto_tfm *tfm,
		       struct scatterlist *dst,
		       struct scatterlist *src,
		       unsigned int nbytes)
{
	return crypt(tfm, dst, src, nbytes,
		     tfm->__crt_alg->cra_cipher.cia_decrypt,
		     ecb_process, 1, NULL);
}

static int cbc_encrypt(struct crypto_tfm *tfm,
		       struct scatterlist *dst,
		       struct scatterlist *src,
		       unsigned int nbytes)
{
	return crypt(tfm, dst, src, nbytes,
		     tfm->__crt_alg->cra_cipher.cia_encrypt,
		     cbc_process, 1, tfm->crt_cipher.cit_iv);
}

static int cbc_encrypt_iv(struct crypto_tfm *tfm,
			  struct scatterlist *dst,
			  struct scatterlist *src,
			  unsigned int nbytes, u8 *iv)
{
	return crypt(tfm, dst, src, nbytes,
		     tfm->__crt_alg->cra_cipher.cia_encrypt,
		     cbc_process, 1, iv);
}

static int cbc_decrypt(struct crypto_tfm *tfm,
		       struct scatterlist *dst,
		       struct scatterlist *src,
		       unsigned int nbytes)
{
	return crypt(tfm, dst, src, nbytes,
		     tfm->__crt_alg->cra_cipher.cia_decrypt,
		     cbc_process, 0, tfm->crt_cipher.cit_iv);
}

static int cbc_decrypt_iv(struct crypto_tfm *tfm,
			  struct scatterlist *dst,
			  struct scatterlist *src,
			  unsigned int nbytes, u8 *iv)
{
	return crypt(tfm, dst, src, nbytes,
		     tfm->__crt_alg->cra_cipher.cia_decrypt,
		     cbc_process, 0, iv);
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
	if (tfm->crt_cipher.cit_iv)
		kfree(tfm->crt_cipher.cit_iv);
}
