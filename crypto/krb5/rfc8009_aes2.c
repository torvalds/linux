// SPDX-License-Identifier: GPL-2.0-or-later
/* rfc8009 AES Encryption with HMAC-SHA2 for Kerberos 5
 *
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <crypto/authenc.h>
#include "internal.h"

static const struct krb5_buffer rfc8009_no_context = { .len = 0, .data = "" };

/*
 * Calculate the key derivation function KDF-HMAC-SHA2(key, label, [context,] k)
 *
 *	KDF-HMAC-SHA2(key, label, [context,] k) = k-truncate(K1)
 *
 *	Using the appropriate one of:
 *		K1 = HMAC-SHA-256(key, 0x00000001 | label | 0x00 | k)
 *		K1 = HMAC-SHA-384(key, 0x00000001 | label | 0x00 | k)
 *		K1 = HMAC-SHA-256(key, 0x00000001 | label | 0x00 | context | k)
 *		K1 = HMAC-SHA-384(key, 0x00000001 | label | 0x00 | context | k)
 *	[rfc8009 sec 3]
 */
static int rfc8009_calc_KDF_HMAC_SHA2(const struct krb5_enctype *krb5,
				      const struct krb5_buffer *key,
				      const struct krb5_buffer *label,
				      const struct krb5_buffer *context,
				      unsigned int k,
				      struct krb5_buffer *result,
				      gfp_t gfp)
{
	struct crypto_shash *shash;
	struct krb5_buffer K1, data;
	struct shash_desc *desc;
	__be32 tmp;
	size_t bsize;
	void *buffer;
	u8 *p;
	int ret = -ENOMEM;

	if (WARN_ON(result->len != k / 8))
		return -EINVAL;

	shash = crypto_alloc_shash(krb5->cksum_name, 0, 0);
	if (IS_ERR(shash))
		return (PTR_ERR(shash) == -ENOENT) ? -ENOPKG : PTR_ERR(shash);
	ret = crypto_shash_setkey(shash, key->data, key->len);
	if (ret < 0)
		goto error_shash;

	ret = -EINVAL;
	if (WARN_ON(crypto_shash_digestsize(shash) * 8 < k))
		goto error_shash;

	ret = -ENOMEM;
	data.len = 4 + label->len + 1 + context->len + 4;
	bsize = krb5_shash_size(shash) +
		krb5_digest_size(shash) +
		crypto_roundup(data.len);
	buffer = kzalloc(bsize, GFP_NOFS);
	if (!buffer)
		goto error_shash;

	desc = buffer;
	desc->tfm = shash;
	ret = crypto_shash_init(desc);
	if (ret < 0)
		goto error;

	p = data.data = buffer +
		krb5_shash_size(shash) +
		krb5_digest_size(shash);
	*(__be32 *)p = htonl(0x00000001);
	p += 4;
	memcpy(p, label->data, label->len);
	p += label->len;
	*p++ = 0;
	memcpy(p, context->data, context->len);
	p += context->len;
	tmp = htonl(k);
	memcpy(p, &tmp, 4);
	p += 4;

	ret = -EINVAL;
	if (WARN_ON(p - (u8 *)data.data != data.len))
		goto error;

	K1.len = crypto_shash_digestsize(shash);
	K1.data = buffer +
		krb5_shash_size(shash);

	ret = crypto_shash_finup(desc, data.data, data.len, K1.data);
	if (ret < 0)
		goto error;

	memcpy(result->data, K1.data, result->len);

error:
	kfree_sensitive(buffer);
error_shash:
	crypto_free_shash(shash);
	return ret;
}

/*
 * Calculate the pseudo-random function, PRF().
 *
 *	PRF = KDF-HMAC-SHA2(input-key, "prf", octet-string, 256)
 *	PRF = KDF-HMAC-SHA2(input-key, "prf", octet-string, 384)
 *
 *      The "prfconstant" used in the PRF operation is the three-octet string
 *      "prf".
 *      [rfc8009 sec 5]
 */
static int rfc8009_calc_PRF(const struct krb5_enctype *krb5,
			    const struct krb5_buffer *input_key,
			    const struct krb5_buffer *octet_string,
			    struct krb5_buffer *result,
			    gfp_t gfp)
{
	static const struct krb5_buffer prfconstant = { 3, "prf" };

	return rfc8009_calc_KDF_HMAC_SHA2(krb5, input_key, &prfconstant,
					  octet_string, krb5->prf_len * 8,
					  result, gfp);
}

/*
 * Derive Ke.
 *	Ke = KDF-HMAC-SHA2(base-key, usage | 0xAA, 128)
 *	Ke = KDF-HMAC-SHA2(base-key, usage | 0xAA, 256)
 *      [rfc8009 sec 5]
 */
static int rfc8009_calc_Ke(const struct krb5_enctype *krb5,
			   const struct krb5_buffer *base_key,
			   const struct krb5_buffer *usage_constant,
			   struct krb5_buffer *result,
			   gfp_t gfp)
{
	return rfc8009_calc_KDF_HMAC_SHA2(krb5, base_key, usage_constant,
					  &rfc8009_no_context, krb5->key_bytes * 8,
					  result, gfp);
}

/*
 * Derive Kc/Ki
 *	Kc = KDF-HMAC-SHA2(base-key, usage | 0x99, 128)
 *	Ki = KDF-HMAC-SHA2(base-key, usage | 0x55, 128)
 *	Kc = KDF-HMAC-SHA2(base-key, usage | 0x99, 192)
 *	Ki = KDF-HMAC-SHA2(base-key, usage | 0x55, 192)
 *      [rfc8009 sec 5]
 */
static int rfc8009_calc_Ki(const struct krb5_enctype *krb5,
			   const struct krb5_buffer *base_key,
			   const struct krb5_buffer *usage_constant,
			   struct krb5_buffer *result,
			   gfp_t gfp)
{
	return rfc8009_calc_KDF_HMAC_SHA2(krb5, base_key, usage_constant,
					  &rfc8009_no_context, krb5->cksum_len * 8,
					  result, gfp);
}

/*
 * Apply encryption and checksumming functions to a message.  Unlike for
 * RFC3961, for RFC8009, we have to chuck the starting IV into the hash first.
 */
static ssize_t rfc8009_encrypt(const struct krb5_enctype *krb5,
			       struct crypto_aead *aead,
			       struct scatterlist *sg, unsigned int nr_sg, size_t sg_len,
			       size_t data_offset, size_t data_len,
			       bool preconfounded)
{
	struct aead_request *req;
	struct scatterlist bsg[2];
	ssize_t ret, done;
	size_t bsize, base_len, secure_offset, secure_len, pad_len, cksum_offset;
	void *buffer;
	u8 *iv, *ad;

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
		krb5_aead_ivsize(aead) * 2;
	buffer = kzalloc(bsize, GFP_NOFS);
	if (!buffer)
		return -ENOMEM;

	req = buffer;
	iv = buffer + krb5_aead_size(aead);
	ad = buffer + krb5_aead_size(aead) + krb5_aead_ivsize(aead);

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

	/* We need to include the starting IV in the hash. */
	sg_init_table(bsg, 2);
	sg_set_buf(&bsg[0], ad, krb5_aead_ivsize(aead));
	sg_chain(bsg, 2, sg);

	/* Hash and encrypt the message. */
	aead_request_set_tfm(req, aead);
	aead_request_set_callback(req, 0, NULL, NULL);
	aead_request_set_ad(req, krb5_aead_ivsize(aead));
	aead_request_set_crypt(req, bsg, bsg, secure_len, iv);
	ret = crypto_aead_encrypt(req);
	if (ret < 0)
		goto error;

	ret = secure_len + krb5->cksum_len;

error:
	kfree_sensitive(buffer);
	return ret;
}

/*
 * Apply decryption and checksumming functions to a message.  Unlike for
 * RFC3961, for RFC8009, we have to chuck the starting IV into the hash first.
 *
 * The offset and length are updated to reflect the actual content of the
 * encrypted region.
 */
static int rfc8009_decrypt(const struct krb5_enctype *krb5,
			   struct crypto_aead *aead,
			   struct scatterlist *sg, unsigned int nr_sg,
			   size_t *_offset, size_t *_len)
{
	struct aead_request *req;
	struct scatterlist bsg[2];
	size_t bsize;
	void *buffer;
	int ret;
	u8 *iv, *ad;

	if (WARN_ON(*_offset != 0))
		return -EINVAL; /* Can't set offset on aead */

	if (*_len < krb5->conf_len + krb5->cksum_len)
		return -EPROTO;

	bsize = krb5_aead_size(aead) +
		krb5_aead_ivsize(aead) * 2;
	buffer = kzalloc(bsize, GFP_NOFS);
	if (!buffer)
		return -ENOMEM;

	req = buffer;
	iv = buffer + krb5_aead_size(aead);
	ad = buffer + krb5_aead_size(aead) + krb5_aead_ivsize(aead);

	/* We need to include the starting IV in the hash. */
	sg_init_table(bsg, 2);
	sg_set_buf(&bsg[0], ad, krb5_aead_ivsize(aead));
	sg_chain(bsg, 2, sg);

	/* Decrypt the message and verify its checksum. */
	aead_request_set_tfm(req, aead);
	aead_request_set_callback(req, 0, NULL, NULL);
	aead_request_set_ad(req, krb5_aead_ivsize(aead));
	aead_request_set_crypt(req, bsg, bsg, *_len, iv);
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

static const struct krb5_crypto_profile rfc8009_crypto_profile = {
	.calc_PRF		= rfc8009_calc_PRF,
	.calc_Kc		= rfc8009_calc_Ki,
	.calc_Ke		= rfc8009_calc_Ke,
	.calc_Ki		= rfc8009_calc_Ki,
	.derive_encrypt_keys	= authenc_derive_encrypt_keys,
	.load_encrypt_keys	= authenc_load_encrypt_keys,
	.derive_checksum_key	= rfc3961_derive_checksum_key,
	.load_checksum_key	= rfc3961_load_checksum_key,
	.encrypt		= rfc8009_encrypt,
	.decrypt		= rfc8009_decrypt,
	.get_mic		= rfc3961_get_mic,
	.verify_mic		= rfc3961_verify_mic,
};

const struct krb5_enctype krb5_aes128_cts_hmac_sha256_128 = {
	.etype		= KRB5_ENCTYPE_AES128_CTS_HMAC_SHA256_128,
	.ctype		= KRB5_CKSUMTYPE_HMAC_SHA256_128_AES128,
	.name		= "aes128-cts-hmac-sha256-128",
	.encrypt_name	= "authenc(hmac(sha256),cts(cbc(aes)))",
	.cksum_name	= "hmac(sha256)",
	.hash_name	= "sha256",
	.derivation_enc	= "cts(cbc(aes))",
	.key_bytes	= 16,
	.key_len	= 16,
	.Kc_len		= 16,
	.Ke_len		= 16,
	.Ki_len		= 16,
	.block_len	= 16,
	.conf_len	= 16,
	.cksum_len	= 16,
	.hash_len	= 20,
	.prf_len	= 32,
	.keyed_cksum	= true,
	.random_to_key	= NULL, /* Identity */
	.profile	= &rfc8009_crypto_profile,
};

const struct krb5_enctype krb5_aes256_cts_hmac_sha384_192 = {
	.etype		= KRB5_ENCTYPE_AES256_CTS_HMAC_SHA384_192,
	.ctype		= KRB5_CKSUMTYPE_HMAC_SHA384_192_AES256,
	.name		= "aes256-cts-hmac-sha384-192",
	.encrypt_name	= "authenc(hmac(sha384),cts(cbc(aes)))",
	.cksum_name	= "hmac(sha384)",
	.hash_name	= "sha384",
	.derivation_enc	= "cts(cbc(aes))",
	.key_bytes	= 32,
	.key_len	= 32,
	.Kc_len		= 24,
	.Ke_len		= 32,
	.Ki_len		= 24,
	.block_len	= 16,
	.conf_len	= 16,
	.cksum_len	= 24,
	.hash_len	= 20,
	.prf_len	= 48,
	.keyed_cksum	= true,
	.random_to_key	= NULL, /* Identity */
	.profile	= &rfc8009_crypto_profile,
};
