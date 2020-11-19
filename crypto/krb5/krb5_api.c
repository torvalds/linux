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
