// SPDX-License-Identifier: BSD-3-Clause
/* rfc3961 Kerberos 5 simplified crypto profile.
 *
 * Parts borrowed from net/sunrpc/auth_gss/.
 */
/*
 * COPYRIGHT (c) 2008
 * The Regents of the University of Michigan
 * ALL RIGHTS RESERVED
 *
 * Permission is granted to use, copy, create derivative works
 * and redistribute this software and such derivative works
 * for any purpose, so long as the name of The University of
 * Michigan is not used in any advertising or publicity
 * pertaining to the use of distribution of this software
 * without specific, written prior authorization.  If the
 * above copyright notice or any other identification of the
 * University of Michigan is included in any copy of any
 * portion of this software, then the disclaimer below must
 * also be included.
 *
 * THIS SOFTWARE IS PROVIDED AS IS, WITHOUT REPRESENTATION
 * FROM THE UNIVERSITY OF MICHIGAN AS TO ITS FITNESS FOR ANY
 * PURPOSE, AND WITHOUT WARRANTY BY THE UNIVERSITY OF
 * MICHIGAN OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * WITHOUT LIMITATION THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE
 * REGENTS OF THE UNIVERSITY OF MICHIGAN SHALL NOT BE LIABLE
 * FOR ANY DAMAGES, INCLUDING SPECIAL, INDIRECT, INCIDENTAL, OR
 * CONSEQUENTIAL DAMAGES, WITH RESPECT TO ANY CLAIM ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OF THE SOFTWARE, EVEN
 * IF IT HAS BEEN OR IS HEREAFTER ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES.
 */

/*
 * Copyright (C) 1998 by the FundsXpress, INC.
 *
 * All rights reserved.
 *
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of FundsXpress. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  FundsXpress makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/lcm.h>
#include <linux/rtnetlink.h>
#include <crypto/authenc.h>
#include <crypto/skcipher.h>
#include <crypto/hash.h>
#include "internal.h"

/* Maximum blocksize for the supported crypto algorithms */
#define KRB5_MAX_BLOCKSIZE  (16)

int crypto_shash_update_sg(struct shash_desc *desc, struct scatterlist *sg,
			   size_t offset, size_t len)
{
	struct sg_mapping_iter miter;
	size_t i, n;
	int ret = 0;

	sg_miter_start(&miter, sg, sg_nents(sg),
		       SG_MITER_FROM_SG | SG_MITER_LOCAL);
	sg_miter_skip(&miter, offset);
	for (i = 0; i < len; i += n) {
		sg_miter_next(&miter);
		n = min(miter.length, len - i);
		ret = crypto_shash_update(desc, miter.addr, n);
		if (ret < 0)
			break;
	}
	sg_miter_stop(&miter);
	return ret;
}

static int rfc3961_do_encrypt(struct crypto_sync_skcipher *tfm, void *iv,
			      const struct krb5_buffer *in, struct krb5_buffer *out)
{
	struct scatterlist sg[1];
	u8 local_iv[KRB5_MAX_BLOCKSIZE] __aligned(KRB5_MAX_BLOCKSIZE) = {0};
	SYNC_SKCIPHER_REQUEST_ON_STACK(req, tfm);
	int ret;

	if (WARN_ON(in->len != out->len))
		return -EINVAL;
	if (out->len % crypto_sync_skcipher_blocksize(tfm) != 0)
		return -EINVAL;

	if (crypto_sync_skcipher_ivsize(tfm) > KRB5_MAX_BLOCKSIZE)
		return -EINVAL;

	if (iv)
		memcpy(local_iv, iv, crypto_sync_skcipher_ivsize(tfm));

	memcpy(out->data, in->data, out->len);
	sg_init_one(sg, out->data, out->len);

	skcipher_request_set_sync_tfm(req, tfm);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, sg, sg, out->len, local_iv);

	ret = crypto_skcipher_encrypt(req);
	skcipher_request_zero(req);
	return ret;
}

/*
 * Calculate an unkeyed basic hash.
 */
static int rfc3961_calc_H(const struct krb5_enctype *krb5,
			  const struct krb5_buffer *data,
			  struct krb5_buffer *digest,
			  gfp_t gfp)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	size_t desc_size;
	int ret = -ENOMEM;

	tfm = crypto_alloc_shash(krb5->hash_name, 0, 0);
	if (IS_ERR(tfm))
		return (PTR_ERR(tfm) == -ENOENT) ? -ENOPKG : PTR_ERR(tfm);

	desc_size = crypto_shash_descsize(tfm) + sizeof(*desc);

	desc = kzalloc(desc_size, gfp);
	if (!desc)
		goto error_tfm;

	digest->len = crypto_shash_digestsize(tfm);
	digest->data = kzalloc(digest->len, gfp);
	if (!digest->data)
		goto error_desc;

	desc->tfm = tfm;
	ret = crypto_shash_init(desc);
	if (ret < 0)
		goto error_digest;

	ret = crypto_shash_finup(desc, data->data, data->len, digest->data);
	if (ret < 0)
		goto error_digest;

	goto error_desc;

error_digest:
	kfree_sensitive(digest->data);
error_desc:
	kfree_sensitive(desc);
error_tfm:
	crypto_free_shash(tfm);
	return ret;
}

/*
 * This is the n-fold function as described in rfc3961, sec 5.1
 * Taken from MIT Kerberos and modified.
 */
static void rfc3961_nfold(const struct krb5_buffer *source, struct krb5_buffer *result)
{
	const u8 *in = source->data;
	u8 *out = result->data;
	unsigned long ulcm;
	unsigned int inbits, outbits;
	int byte, i, msbit;

	/* the code below is more readable if I make these bytes instead of bits */
	inbits = source->len;
	outbits = result->len;

	/* first compute lcm(n,k) */
	ulcm = lcm(inbits, outbits);

	/* now do the real work */
	memset(out, 0, outbits);
	byte = 0;

	/* this will end up cycling through k lcm(k,n)/k times, which
	 * is correct.
	 */
	for (i = ulcm-1; i >= 0; i--) {
		/* compute the msbit in k which gets added into this byte */
		msbit = (
			/* first, start with the msbit in the first,
			 * unrotated byte
			 */
			((inbits << 3) - 1) +
			/* then, for each byte, shift to the right
			 * for each repetition
			 */
			(((inbits << 3) + 13) * (i/inbits)) +
			/* last, pick out the correct byte within
			 * that shifted repetition
			 */
			((inbits - (i % inbits)) << 3)
			 ) % (inbits << 3);

		/* pull out the byte value itself */
		byte += (((in[((inbits - 1) - (msbit >> 3)) % inbits] << 8) |
			  (in[((inbits)     - (msbit >> 3)) % inbits]))
			 >> ((msbit & 7) + 1)) & 0xff;

		/* do the addition */
		byte += out[i % outbits];
		out[i % outbits] = byte & 0xff;

		/* keep around the carry bit, if any */
		byte >>= 8;
	}

	/* if there's a carry bit left over, add it back in */
	if (byte) {
		for (i = outbits - 1; i >= 0; i--) {
			/* do the addition */
			byte += out[i];
			out[i] = byte & 0xff;

			/* keep around the carry bit, if any */
			byte >>= 8;
		}
	}
}

/*
 * Calculate a derived key, DK(Base Key, Well-Known Constant)
 *
 * DK(Key, Constant) = random-to-key(DR(Key, Constant))
 * DR(Key, Constant) = k-truncate(E(Key, Constant, initial-cipher-state))
 * K1 = E(Key, n-fold(Constant), initial-cipher-state)
 * K2 = E(Key, K1, initial-cipher-state)
 * K3 = E(Key, K2, initial-cipher-state)
 * K4 = ...
 * DR(Key, Constant) = k-truncate(K1 | K2 | K3 | K4 ...)
 * [rfc3961 sec 5.1]
 */
static int rfc3961_calc_DK(const struct krb5_enctype *krb5,
			   const struct krb5_buffer *inkey,
			   const struct krb5_buffer *in_constant,
			   struct krb5_buffer *result,
			   gfp_t gfp)
{
	unsigned int blocksize, keybytes, keylength, n;
	struct krb5_buffer inblock, outblock, rawkey;
	struct crypto_sync_skcipher *cipher;
	int ret = -EINVAL;

	blocksize = krb5->block_len;
	keybytes = krb5->key_bytes;
	keylength = krb5->key_len;

	if (inkey->len != keylength || result->len != keylength)
		return -EINVAL;
	if (!krb5->random_to_key && result->len != keybytes)
		return -EINVAL;

	cipher = crypto_alloc_sync_skcipher(krb5->derivation_enc, 0, 0);
	if (IS_ERR(cipher)) {
		ret = (PTR_ERR(cipher) == -ENOENT) ? -ENOPKG : PTR_ERR(cipher);
		goto err_return;
	}
	ret = crypto_sync_skcipher_setkey(cipher, inkey->data, inkey->len);
	if (ret < 0)
		goto err_free_cipher;

	ret = -ENOMEM;
	inblock.data = kzalloc(blocksize * 2 + keybytes, gfp);
	if (!inblock.data)
		goto err_free_cipher;

	inblock.len	= blocksize;
	outblock.data	= inblock.data + blocksize;
	outblock.len	= blocksize;
	rawkey.data	= outblock.data + blocksize;
	rawkey.len	= keybytes;

	/* initialize the input block */

	if (in_constant->len == inblock.len)
		memcpy(inblock.data, in_constant->data, inblock.len);
	else
		rfc3961_nfold(in_constant, &inblock);

	/* loop encrypting the blocks until enough key bytes are generated */
	n = 0;
	while (n < rawkey.len) {
		rfc3961_do_encrypt(cipher, NULL, &inblock, &outblock);

		if (keybytes - n <= outblock.len) {
			memcpy(rawkey.data + n, outblock.data, keybytes - n);
			break;
		}

		memcpy(rawkey.data + n, outblock.data, outblock.len);
		memcpy(inblock.data, outblock.data, outblock.len);
		n += outblock.len;
	}

	/* postprocess the key */
	if (!krb5->random_to_key) {
		/* Identity random-to-key function. */
		memcpy(result->data, rawkey.data, rawkey.len);
		ret = 0;
	} else {
		ret = krb5->random_to_key(krb5, &rawkey, result);
	}

	kfree_sensitive(inblock.data);
err_free_cipher:
	crypto_free_sync_skcipher(cipher);
err_return:
	return ret;
}

/*
 * Calculate single encryption, E()
 *
 *	E(Key, octets)
 */
static int rfc3961_calc_E(const struct krb5_enctype *krb5,
			  const struct krb5_buffer *key,
			  const struct krb5_buffer *in_data,
			  struct krb5_buffer *result,
			  gfp_t gfp)
{
	struct crypto_sync_skcipher *cipher;
	int ret;

	cipher = crypto_alloc_sync_skcipher(krb5->derivation_enc, 0, 0);
	if (IS_ERR(cipher)) {
		ret = (PTR_ERR(cipher) == -ENOENT) ? -ENOPKG : PTR_ERR(cipher);
		goto err;
	}

	ret = crypto_sync_skcipher_setkey(cipher, key->data, key->len);
	if (ret < 0)
		goto err_free;

	ret = rfc3961_do_encrypt(cipher, NULL, in_data, result);

err_free:
	crypto_free_sync_skcipher(cipher);
err:
	return ret;
}

/*
 * Calculate the pseudo-random function, PRF().
 *
 *      tmp1 = H(octet-string)
 *      tmp2 = truncate tmp1 to multiple of m
 *      PRF = E(DK(protocol-key, prfconstant), tmp2, initial-cipher-state)
 *
 *      The "prfconstant" used in the PRF operation is the three-octet string
 *      "prf".
 *      [rfc3961 sec 5.3]
 */
static int rfc3961_calc_PRF(const struct krb5_enctype *krb5,
			    const struct krb5_buffer *protocol_key,
			    const struct krb5_buffer *octet_string,
			    struct krb5_buffer *result,
			    gfp_t gfp)
{
	static const struct krb5_buffer prfconstant = { 3, "prf" };
	struct krb5_buffer derived_key;
	struct krb5_buffer tmp1, tmp2;
	unsigned int m = krb5->block_len;
	void *buffer;
	int ret;

	if (result->len != krb5->prf_len)
		return -EINVAL;

	tmp1.len = krb5->hash_len;
	derived_key.len = krb5->key_bytes;
	buffer = kzalloc(round16(tmp1.len) + round16(derived_key.len), gfp);
	if (!buffer)
		return -ENOMEM;

	tmp1.data = buffer;
	derived_key.data = buffer + round16(tmp1.len);

	ret = rfc3961_calc_H(krb5, octet_string, &tmp1, gfp);
	if (ret < 0)
		goto err;

	tmp2.len = tmp1.len & ~(m - 1);
	tmp2.data = tmp1.data;

	ret = rfc3961_calc_DK(krb5, protocol_key, &prfconstant, &derived_key, gfp);
	if (ret < 0)
		goto err;

	ret = rfc3961_calc_E(krb5, &derived_key, &tmp2, result, gfp);

err:
	kfree_sensitive(buffer);
	return ret;
}

/*
 * Derive the Ke and Ki keys and package them into a key parameter that can be
 * given to the setkey of a authenc AEAD crypto object.
 */
int authenc_derive_encrypt_keys(const struct krb5_enctype *krb5,
				const struct krb5_buffer *TK,
				unsigned int usage,
				struct krb5_buffer *setkey,
				gfp_t gfp)
{
	struct crypto_authenc_key_param *param;
	struct krb5_buffer Ke, Ki;
	struct rtattr *rta;
	int ret;

	Ke.len  = krb5->Ke_len;
	Ki.len  = krb5->Ki_len;
	setkey->len = RTA_LENGTH(sizeof(*param)) + Ke.len + Ki.len;
	setkey->data = kzalloc(setkey->len, GFP_KERNEL);
	if (!setkey->data)
		return -ENOMEM;

	rta = setkey->data;
	rta->rta_type = CRYPTO_AUTHENC_KEYA_PARAM;
	rta->rta_len = RTA_LENGTH(sizeof(*param));
	param = RTA_DATA(rta);
	param->enckeylen = htonl(Ke.len);

	Ki.data = (void *)(param + 1);
	Ke.data = Ki.data + Ki.len;

	ret = krb5_derive_Ke(krb5, TK, usage, &Ke, gfp);
	if (ret < 0) {
		pr_err("get_Ke failed %d\n", ret);
		return ret;
	}
	ret = krb5_derive_Ki(krb5, TK, usage, &Ki, gfp);
	if (ret < 0)
		pr_err("get_Ki failed %d\n", ret);
	return ret;
}

/*
 * Package predefined Ke and Ki keys and into a key parameter that can be given
 * to the setkey of an authenc AEAD crypto object.
 */
int authenc_load_encrypt_keys(const struct krb5_enctype *krb5,
			      const struct krb5_buffer *Ke,
			      const struct krb5_buffer *Ki,
			      struct krb5_buffer *setkey,
			      gfp_t gfp)
{
	struct crypto_authenc_key_param *param;
	struct rtattr *rta;

	setkey->len = RTA_LENGTH(sizeof(*param)) + Ke->len + Ki->len;
	setkey->data = kzalloc(setkey->len, GFP_KERNEL);
	if (!setkey->data)
		return -ENOMEM;

	rta = setkey->data;
	rta->rta_type = CRYPTO_AUTHENC_KEYA_PARAM;
	rta->rta_len = RTA_LENGTH(sizeof(*param));
	param = RTA_DATA(rta);
	param->enckeylen = htonl(Ke->len);
	memcpy((void *)(param + 1), Ki->data, Ki->len);
	memcpy((void *)(param + 1) + Ki->len, Ke->data, Ke->len);
	return 0;
}

/*
 * Derive the Kc key for checksum-only mode and package it into a key parameter
 * that can be given to the setkey of a hash crypto object.
 */
int rfc3961_derive_checksum_key(const struct krb5_enctype *krb5,
				const struct krb5_buffer *TK,
				unsigned int usage,
				struct krb5_buffer *setkey,
				gfp_t gfp)
{
	int ret;

	setkey->len = krb5->Kc_len;
	setkey->data = kzalloc(setkey->len, GFP_KERNEL);
	if (!setkey->data)
		return -ENOMEM;

	ret = krb5_derive_Kc(krb5, TK, usage, setkey, gfp);
	if (ret < 0)
		pr_err("get_Kc failed %d\n", ret);
	return ret;
}

/*
 * Package a predefined Kc key for checksum-only mode into a key parameter that
 * can be given to the setkey of a hash crypto object.
 */
int rfc3961_load_checksum_key(const struct krb5_enctype *krb5,
			      const struct krb5_buffer *Kc,
			      struct krb5_buffer *setkey,
			      gfp_t gfp)
{
	setkey->len = krb5->Kc_len;
	setkey->data = kmemdup(Kc->data, Kc->len, GFP_KERNEL);
	if (!setkey->data)
		return -ENOMEM;
	return 0;
}

/*
 * Apply encryption and checksumming functions to part of a scatterlist.
 */
ssize_t krb5_aead_encrypt(const struct krb5_enctype *krb5,
			  struct crypto_aead *aead,
			  struct scatterlist *sg, unsigned int nr_sg, size_t sg_len,
			  size_t data_offset, size_t data_len,
			  bool preconfounded)
{
	struct aead_request *req;
	ssize_t ret, done;
	size_t bsize, base_len, secure_offset, secure_len, pad_len, cksum_offset;
	void *buffer;
	u8 *iv;

	if (WARN_ON(data_offset != krb5->conf_len))
		return -EINVAL; /* Data is in wrong place */

	secure_offset	= 0;
	base_len	= krb5->conf_len + data_len;
	pad_len		= 0;
	secure_len	= base_len + pad_len;
	cksum_offset	= secure_len;
	if (WARN_ON(cksum_offset + krb5->cksum_len > sg_len))
		return -EFAULT;

	bsize = krb5_aead_size(aead) +
		krb5_aead_ivsize(aead);
	buffer = kzalloc(bsize, GFP_NOFS);
	if (!buffer)
		return -ENOMEM;

	/* Insert the confounder into the buffer */
	ret = -EFAULT;
	if (!preconfounded) {
		get_random_bytes(buffer, krb5->conf_len);
		done = sg_pcopy_from_buffer(sg, nr_sg, buffer, krb5->conf_len,
					    secure_offset);
		if (done != krb5->conf_len)
			goto error;
	}

	/* We may need to pad out to the crypto blocksize. */
	if (pad_len) {
		done = sg_zero_buffer(sg, nr_sg, pad_len, data_offset + data_len);
		if (done != pad_len)
			goto error;
	}

	/* Hash and encrypt the message. */
	req = buffer;
	iv = buffer + krb5_aead_size(aead);

	aead_request_set_tfm(req, aead);
	aead_request_set_callback(req, 0, NULL, NULL);
	aead_request_set_crypt(req, sg, sg, secure_len, iv);
	ret = crypto_aead_encrypt(req);
	if (ret < 0)
		goto error;

	ret = secure_len + krb5->cksum_len;

error:
	kfree_sensitive(buffer);
	return ret;
}

/*
 * Apply decryption and checksumming functions to a message.  The offset and
 * length are updated to reflect the actual content of the encrypted region.
 */
int krb5_aead_decrypt(const struct krb5_enctype *krb5,
		      struct crypto_aead *aead,
		      struct scatterlist *sg, unsigned int nr_sg,
		      size_t *_offset, size_t *_len)
{
	struct aead_request *req;
	size_t bsize;
	void *buffer;
	int ret;
	u8 *iv;

	if (WARN_ON(*_offset != 0))
		return -EINVAL; /* Can't set offset on aead */

	if (*_len < krb5->conf_len + krb5->cksum_len)
		return -EPROTO;

	bsize = krb5_aead_size(aead) +
		krb5_aead_ivsize(aead);
	buffer = kzalloc(bsize, GFP_NOFS);
	if (!buffer)
		return -ENOMEM;

	/* Decrypt the message and verify its checksum. */
	req = buffer;
	iv = buffer + krb5_aead_size(aead);

	aead_request_set_tfm(req, aead);
	aead_request_set_callback(req, 0, NULL, NULL);
	aead_request_set_crypt(req, sg, sg, *_len, iv);
	ret = crypto_aead_decrypt(req);
	if (ret < 0)
		goto error;

	/* Adjust the boundaries of the data. */
	*_offset += krb5->conf_len;
	*_len -= krb5->conf_len + krb5->cksum_len;
	ret = 0;

error:
	kfree_sensitive(buffer);
	return ret;
}

/*
 * Generate a checksum over some metadata and part of an skbuff and insert the
 * MIC into the skbuff immediately prior to the data.
 */
ssize_t rfc3961_get_mic(const struct krb5_enctype *krb5,
			struct crypto_shash *shash,
			const struct krb5_buffer *metadata,
			struct scatterlist *sg, unsigned int nr_sg, size_t sg_len,
			size_t data_offset, size_t data_len)
{
	struct shash_desc *desc;
	ssize_t ret, done;
	size_t bsize;
	void *buffer, *digest;

	if (WARN_ON(data_offset != krb5->cksum_len))
		return -EMSGSIZE;

	bsize = krb5_shash_size(shash) +
		krb5_digest_size(shash);
	buffer = kzalloc(bsize, GFP_NOFS);
	if (!buffer)
		return -ENOMEM;

	/* Calculate the MIC with key Kc and store it into the skb */
	desc = buffer;
	desc->tfm = shash;
	ret = crypto_shash_init(desc);
	if (ret < 0)
		goto error;

	if (metadata) {
		ret = crypto_shash_update(desc, metadata->data, metadata->len);
		if (ret < 0)
			goto error;
	}

	ret = crypto_shash_update_sg(desc, sg, data_offset, data_len);
	if (ret < 0)
		goto error;

	digest = buffer + krb5_shash_size(shash);
	ret = crypto_shash_final(desc, digest);
	if (ret < 0)
		goto error;

	ret = -EFAULT;
	done = sg_pcopy_from_buffer(sg, nr_sg, digest, krb5->cksum_len,
				    data_offset - krb5->cksum_len);
	if (done != krb5->cksum_len)
		goto error;

	ret = krb5->cksum_len + data_len;

error:
	kfree_sensitive(buffer);
	return ret;
}

/*
 * Check the MIC on a region of an skbuff.  The offset and length are updated
 * to reflect the actual content of the secure region.
 */
int rfc3961_verify_mic(const struct krb5_enctype *krb5,
		       struct crypto_shash *shash,
		       const struct krb5_buffer *metadata,
		       struct scatterlist *sg, unsigned int nr_sg,
		       size_t *_offset, size_t *_len)
{
	struct shash_desc *desc;
	ssize_t done;
	size_t bsize, data_offset, data_len, offset = *_offset, len = *_len;
	void *buffer = NULL;
	int ret;
	u8 *cksum, *cksum2;

	if (len < krb5->cksum_len)
		return -EPROTO;
	data_offset = offset + krb5->cksum_len;
	data_len = len - krb5->cksum_len;

	bsize = krb5_shash_size(shash) +
		krb5_digest_size(shash) * 2;
	buffer = kzalloc(bsize, GFP_NOFS);
	if (!buffer)
		return -ENOMEM;

	cksum = buffer +
		krb5_shash_size(shash);
	cksum2 = buffer +
		krb5_shash_size(shash) +
		krb5_digest_size(shash);

	/* Calculate the MIC */
	desc = buffer;
	desc->tfm = shash;
	ret = crypto_shash_init(desc);
	if (ret < 0)
		goto error;

	if (metadata) {
		ret = crypto_shash_update(desc, metadata->data, metadata->len);
		if (ret < 0)
			goto error;
	}

	crypto_shash_update_sg(desc, sg, data_offset, data_len);
	crypto_shash_final(desc, cksum);

	ret = -EFAULT;
	done = sg_pcopy_to_buffer(sg, nr_sg, cksum2, krb5->cksum_len, offset);
	if (done != krb5->cksum_len)
		goto error;

	if (memcmp(cksum, cksum2, krb5->cksum_len) != 0) {
		ret = -EBADMSG;
		goto error;
	}

	*_offset += krb5->cksum_len;
	*_len -= krb5->cksum_len;
	ret = 0;

error:
	kfree_sensitive(buffer);
	return ret;
}

const struct krb5_crypto_profile rfc3961_simplified_profile = {
	.calc_PRF		= rfc3961_calc_PRF,
	.calc_Kc		= rfc3961_calc_DK,
	.calc_Ke		= rfc3961_calc_DK,
	.calc_Ki		= rfc3961_calc_DK,
	.derive_encrypt_keys	= authenc_derive_encrypt_keys,
	.load_encrypt_keys	= authenc_load_encrypt_keys,
	.derive_checksum_key	= rfc3961_derive_checksum_key,
	.load_checksum_key	= rfc3961_load_checksum_key,
	.encrypt		= krb5_aead_encrypt,
	.decrypt		= krb5_aead_decrypt,
	.get_mic		= rfc3961_get_mic,
	.verify_mic		= rfc3961_verify_mic,
};
