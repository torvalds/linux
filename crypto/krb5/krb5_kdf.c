// SPDX-License-Identifier: GPL-2.0-or-later
/* Kerberos key derivation.
 *
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/export.h>
#include <linux/slab.h>
#include <crypto/skcipher.h>
#include <crypto/hash.h>
#include "internal.h"

/**
 * crypto_krb5_calc_PRFplus - Calculate PRF+ [RFC4402]
 * @krb5: The encryption type to use
 * @K: The protocol key for the pseudo-random function
 * @L: The length of the output
 * @S: The input octet string
 * @result: Result buffer, sized to krb5->prf_len
 * @gfp: Allocation restrictions
 *
 * Calculate the kerberos pseudo-random function, PRF+() by the following
 * method:
 *
 *      PRF+(K, L, S) = truncate(L, T1 || T2 || .. || Tn)
 *      Tn = PRF(K, n || S)
 *      [rfc4402 sec 2]
 */
int crypto_krb5_calc_PRFplus(const struct krb5_enctype *krb5,
			     const struct krb5_buffer *K,
			     unsigned int L,
			     const struct krb5_buffer *S,
			     struct krb5_buffer *result,
			     gfp_t gfp)
{
	struct krb5_buffer T_series, Tn, n_S;
	void *buffer;
	int ret, n = 1;

	Tn.len = krb5->prf_len;
	T_series.len = 0;
	n_S.len = 4 + S->len;

	buffer = kzalloc(round16(L + Tn.len) + round16(n_S.len), gfp);
	if (!buffer)
		return -ENOMEM;

	T_series.data = buffer;
	n_S.data = buffer + round16(L + Tn.len);
	memcpy(n_S.data + 4, S->data, S->len);

	while (T_series.len < L) {
		*(__be32 *)(n_S.data) = htonl(n);
		Tn.data = T_series.data + Tn.len * (n - 1);
		ret = krb5->profile->calc_PRF(krb5, K, &n_S, &Tn, gfp);
		if (ret < 0)
			goto err;
		T_series.len += Tn.len;
		n++;
	}

	/* Truncate to L */
	memcpy(result->data, T_series.data, L);
	ret = 0;

err:
	kfree_sensitive(buffer);
	return ret;
}
EXPORT_SYMBOL(crypto_krb5_calc_PRFplus);

/**
 * krb5_derive_Kc - Derive key Kc and install into a hash
 * @krb5: The encryption type to use
 * @TK: The base key
 * @usage: The key usage number
 * @key: Prepped buffer to store the key into
 * @gfp: Allocation restrictions
 *
 * Derive the Kerberos Kc checksumming key.  The key is stored into the
 * prepared buffer.
 */
int krb5_derive_Kc(const struct krb5_enctype *krb5, const struct krb5_buffer *TK,
		   u32 usage, struct krb5_buffer *key, gfp_t gfp)
{
	u8 buf[5] __aligned(CRYPTO_MINALIGN);
	struct krb5_buffer usage_constant = { .len = 5, .data = buf };

	*(__be32 *)buf = cpu_to_be32(usage);
	buf[4] = KEY_USAGE_SEED_CHECKSUM;

	key->len = krb5->Kc_len;
	return krb5->profile->calc_Kc(krb5, TK, &usage_constant, key, gfp);
}

/**
 * krb5_derive_Ke - Derive key Ke and install into an skcipher
 * @krb5: The encryption type to use
 * @TK: The base key
 * @usage: The key usage number
 * @key: Prepped buffer to store the key into
 * @gfp: Allocation restrictions
 *
 * Derive the Kerberos Ke encryption key.  The key is stored into the prepared
 * buffer.
 */
int krb5_derive_Ke(const struct krb5_enctype *krb5, const struct krb5_buffer *TK,
		   u32 usage, struct krb5_buffer *key, gfp_t gfp)
{
	u8 buf[5] __aligned(CRYPTO_MINALIGN);
	struct krb5_buffer usage_constant = { .len = 5, .data = buf };

	*(__be32 *)buf = cpu_to_be32(usage);
	buf[4] = KEY_USAGE_SEED_ENCRYPTION;

	key->len = krb5->Ke_len;
	return krb5->profile->calc_Ke(krb5, TK, &usage_constant, key, gfp);
}

/**
 * krb5_derive_Ki - Derive key Ki and install into a hash
 * @krb5: The encryption type to use
 * @TK: The base key
 * @usage: The key usage number
 * @key: Prepped buffer to store the key into
 * @gfp: Allocation restrictions
 *
 * Derive the Kerberos Ki integrity checksum key.  The key is stored into the
 * prepared buffer.
 */
int krb5_derive_Ki(const struct krb5_enctype *krb5, const struct krb5_buffer *TK,
		   u32 usage, struct krb5_buffer *key, gfp_t gfp)
{
	u8 buf[5] __aligned(CRYPTO_MINALIGN);
	struct krb5_buffer usage_constant = { .len = 5, .data = buf };

	*(__be32 *)buf = cpu_to_be32(usage);
	buf[4] = KEY_USAGE_SEED_INTEGRITY;

	key->len = krb5->Ki_len;
	return krb5->profile->calc_Ki(krb5, TK, &usage_constant, key, gfp);
}
