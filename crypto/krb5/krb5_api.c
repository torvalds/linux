// SPDX-License-Identifier: GPL-2.0-or-later
/* Kerberos 5 crypto library.
 *
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include "internal.h"

MODULE_DESCRIPTION("Kerberos 5 crypto");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

static const struct krb5_enctype *const krb5_supported_enctypes[] = {
};

/**
 * crypto_krb5_find_enctype - Find the handler for a Kerberos5 encryption type
 * @enctype: The standard Kerberos encryption type number
 *
 * Look up a Kerberos encryption type by number.  If successful, returns a
 * pointer to the type tables; returns NULL otherwise.
 */
const struct krb5_enctype *crypto_krb5_find_enctype(u32 enctype)
{
	const struct krb5_enctype *krb5;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(krb5_supported_enctypes); i++) {
		krb5 = krb5_supported_enctypes[i];
		if (krb5->etype == enctype)
			return krb5;
	}

	return NULL;
}
EXPORT_SYMBOL(crypto_krb5_find_enctype);

/**
 * crypto_krb5_how_much_buffer - Work out how much buffer is required for an amount of data
 * @krb5: The encoding to use.
 * @mode: The mode in which to operated (checksum/encrypt)
 * @data_size: How much data we want to allow for
 * @_offset: Where to place the offset into the buffer
 *
 * Calculate how much buffer space is required to wrap a given amount of data.
 * This allows for a confounder, padding and checksum as appropriate.  The
 * amount of buffer required is returned and the offset into the buffer at
 * which the data will start is placed in *_offset.
 */
size_t crypto_krb5_how_much_buffer(const struct krb5_enctype *krb5,
				   enum krb5_crypto_mode mode,
				   size_t data_size, size_t *_offset)
{
	switch (mode) {
	case KRB5_CHECKSUM_MODE:
		*_offset = krb5->cksum_len;
		return krb5->cksum_len + data_size;

	case KRB5_ENCRYPT_MODE:
		*_offset = krb5->conf_len;
		return krb5->conf_len + data_size + krb5->cksum_len;

	default:
		WARN_ON(1);
		*_offset = 0;
		return 0;
	}
}
EXPORT_SYMBOL(crypto_krb5_how_much_buffer);

/**
 * crypto_krb5_how_much_data - Work out how much data can fit in an amount of buffer
 * @krb5: The encoding to use.
 * @mode: The mode in which to operated (checksum/encrypt)
 * @_buffer_size: How much buffer we want to allow for (may be reduced)
 * @_offset: Where to place the offset into the buffer
 *
 * Calculate how much data can be fitted into given amount of buffer.  This
 * allows for a confounder, padding and checksum as appropriate.  The amount of
 * data that will fit is returned, the amount of buffer required is shrunk to
 * allow for alignment and the offset into the buffer at which the data will
 * start is placed in *_offset.
 */
size_t crypto_krb5_how_much_data(const struct krb5_enctype *krb5,
				 enum krb5_crypto_mode mode,
				 size_t *_buffer_size, size_t *_offset)
{
	size_t buffer_size = *_buffer_size, data_size;

	switch (mode) {
	case KRB5_CHECKSUM_MODE:
		if (WARN_ON(buffer_size < krb5->cksum_len + 1))
			goto bad;
		*_offset = krb5->cksum_len;
		return buffer_size - krb5->cksum_len;

	case KRB5_ENCRYPT_MODE:
		if (WARN_ON(buffer_size < krb5->conf_len + 1 + krb5->cksum_len))
			goto bad;
		data_size = buffer_size - krb5->cksum_len;
		*_offset = krb5->conf_len;
		return data_size - krb5->conf_len;

	default:
		WARN_ON(1);
		goto bad;
	}

bad:
	*_offset = 0;
	return 0;
}
EXPORT_SYMBOL(crypto_krb5_how_much_data);

/**
 * crypto_krb5_where_is_the_data - Find the data in a decrypted message
 * @krb5: The encoding to use.
 * @mode: Mode of operation
 * @_offset: Offset of the secure blob in the buffer; updated to data offset.
 * @_len: The length of the secure blob; updated to data length.
 *
 * Find the offset and size of the data in a secure message so that this
 * information can be used in the metadata buffer which will get added to the
 * digest by crypto_krb5_verify_mic().
 */
void crypto_krb5_where_is_the_data(const struct krb5_enctype *krb5,
				   enum krb5_crypto_mode mode,
				   size_t *_offset, size_t *_len)
{
	switch (mode) {
	case KRB5_CHECKSUM_MODE:
		*_offset += krb5->cksum_len;
		*_len -= krb5->cksum_len;
		return;
	case KRB5_ENCRYPT_MODE:
		*_offset += krb5->conf_len;
		*_len -= krb5->conf_len + krb5->cksum_len;
		return;
	default:
		WARN_ON_ONCE(1);
		return;
	}
}
EXPORT_SYMBOL(crypto_krb5_where_is_the_data);

/*
 * Prepare the encryption with derived key data.
 */
struct crypto_aead *krb5_prepare_encryption(const struct krb5_enctype *krb5,
					    const struct krb5_buffer *keys,
					    gfp_t gfp)
{
	struct crypto_aead *ci = NULL;
	int ret = -ENOMEM;

	ci = crypto_alloc_aead(krb5->encrypt_name, 0, 0);
	if (IS_ERR(ci)) {
		ret = PTR_ERR(ci);
		if (ret == -ENOENT)
			ret = -ENOPKG;
		goto err;
	}

	ret = crypto_aead_setkey(ci, keys->data, keys->len);
	if (ret < 0) {
		pr_err("Couldn't set AEAD key %s: %d\n", krb5->encrypt_name, ret);
		goto err_ci;
	}

	ret = crypto_aead_setauthsize(ci, krb5->cksum_len);
	if (ret < 0) {
		pr_err("Couldn't set AEAD authsize %s: %d\n", krb5->encrypt_name, ret);
		goto err_ci;
	}

	return ci;
err_ci:
	crypto_free_aead(ci);
err:
	return ERR_PTR(ret);
}

/**
 * crypto_krb5_prepare_encryption - Prepare AEAD crypto object for encryption-mode
 * @krb5: The encoding to use.
 * @TK: The transport key to use.
 * @usage: The usage constant for key derivation.
 * @gfp: Allocation flags.
 *
 * Allocate a crypto object that does all the necessary crypto, key it and set
 * its parameters and return the crypto handle to it.  This can then be used to
 * dispatch encrypt and decrypt operations.
 */
struct crypto_aead *crypto_krb5_prepare_encryption(const struct krb5_enctype *krb5,
						   const struct krb5_buffer *TK,
						   u32 usage, gfp_t gfp)
{
	struct crypto_aead *ci = NULL;
	struct krb5_buffer keys = {};
	int ret;

	ret = krb5->profile->derive_encrypt_keys(krb5, TK, usage, &keys, gfp);
	if (ret < 0)
		goto err;

	ci = krb5_prepare_encryption(krb5, &keys, gfp);
	if (IS_ERR(ci)) {
		ret = PTR_ERR(ci);
		goto err;
	}

	kfree(keys.data);
	return ci;
err:
	kfree(keys.data);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(crypto_krb5_prepare_encryption);

/*
 * Prepare the checksum with derived key data.
 */
struct crypto_shash *krb5_prepare_checksum(const struct krb5_enctype *krb5,
					   const struct krb5_buffer *Kc,
					   gfp_t gfp)
{
	struct crypto_shash *ci = NULL;
	int ret = -ENOMEM;

	ci = crypto_alloc_shash(krb5->cksum_name, 0, 0);
	if (IS_ERR(ci)) {
		ret = PTR_ERR(ci);
		if (ret == -ENOENT)
			ret = -ENOPKG;
		goto err;
	}

	ret = crypto_shash_setkey(ci, Kc->data, Kc->len);
	if (ret < 0) {
		pr_err("Couldn't set shash key %s: %d\n", krb5->cksum_name, ret);
		goto err_ci;
	}

	return ci;
err_ci:
	crypto_free_shash(ci);
err:
	return ERR_PTR(ret);
}

/**
 * crypto_krb5_prepare_checksum - Prepare AEAD crypto object for checksum-mode
 * @krb5: The encoding to use.
 * @TK: The transport key to use.
 * @usage: The usage constant for key derivation.
 * @gfp: Allocation flags.
 *
 * Allocate a crypto object that does all the necessary crypto, key it and set
 * its parameters and return the crypto handle to it.  This can then be used to
 * dispatch get_mic and verify_mic operations.
 */
struct crypto_shash *crypto_krb5_prepare_checksum(const struct krb5_enctype *krb5,
						  const struct krb5_buffer *TK,
						  u32 usage, gfp_t gfp)
{
	struct crypto_shash *ci = NULL;
	struct krb5_buffer keys = {};
	int ret;

	ret = krb5->profile->derive_checksum_key(krb5, TK, usage, &keys, gfp);
	if (ret < 0) {
		pr_err("get_Kc failed %d\n", ret);
		goto err;
	}

	ci = krb5_prepare_checksum(krb5, &keys, gfp);
	if (IS_ERR(ci)) {
		ret = PTR_ERR(ci);
		goto err;
	}

	kfree(keys.data);
	return ci;
err:
	kfree(keys.data);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(crypto_krb5_prepare_checksum);
