/*
 * PKCS #1 (RSA Encryption)
 * Copyright (c) 2006-2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"
#include "rsa.h"
#include "pkcs1.h"


static int pkcs1_generate_encryption_block(u8 block_type, size_t modlen,
					   const u8 *in, size_t inlen,
					   u8 *out, size_t *outlen)
{
	size_t ps_len;
	u8 *pos;

	/*
	 * PKCS #1 v1.5, 8.1:
	 *
	 * EB = 00 || BT || PS || 00 || D
	 * BT = 00 or 01 for private-key operation; 02 for public-key operation
	 * PS = k-3-||D||; at least eight octets
	 * (BT=0: PS=0x00, BT=1: PS=0xff, BT=2: PS=pseudorandom non-zero)
	 * k = length of modulus in octets (modlen)
	 */

	if (modlen < 12 || modlen > *outlen || inlen > modlen - 11) {
		wpa_printf(MSG_DEBUG, "PKCS #1: %s - Invalid buffer "
			   "lengths (modlen=%lu outlen=%lu inlen=%lu)",
			   __func__, (unsigned long) modlen,
			   (unsigned long) *outlen,
			   (unsigned long) inlen);
		return -1;
	}

	pos = out;
	*pos++ = 0x00;
	*pos++ = block_type; /* BT */
	ps_len = modlen - inlen - 3;
	switch (block_type) {
	case 0:
		os_memset(pos, 0x00, ps_len);
		pos += ps_len;
		break;
	case 1:
		os_memset(pos, 0xff, ps_len);
		pos += ps_len;
		break;
	case 2:
		if (os_get_random(pos, ps_len) < 0) {
			wpa_printf(MSG_DEBUG, "PKCS #1: %s - Failed to get "
				   "random data for PS", __func__);
			return -1;
		}
		while (ps_len--) {
			if (*pos == 0x00)
				*pos = 0x01;
			pos++;
		}
		break;
	default:
		wpa_printf(MSG_DEBUG, "PKCS #1: %s - Unsupported block type "
			   "%d", __func__, block_type);
		return -1;
	}
	*pos++ = 0x00;
	os_memcpy(pos, in, inlen); /* D */

	return 0;
}


int pkcs1_encrypt(int block_type, struct crypto_rsa_key *key,
		  int use_private, const u8 *in, size_t inlen,
		  u8 *out, size_t *outlen)
{
	size_t modlen;

	modlen = crypto_rsa_get_modulus_len(key);

	if (pkcs1_generate_encryption_block(block_type, modlen, in, inlen,
					    out, outlen) < 0)
		return -1;

	return crypto_rsa_exptmod(out, modlen, out, outlen, key, use_private);
}


int pkcs1_v15_private_key_decrypt(struct crypto_rsa_key *key,
				  const u8 *in, size_t inlen,
				  u8 *out, size_t *outlen)
{
	int res;
	u8 *pos, *end;

	res = crypto_rsa_exptmod(in, inlen, out, outlen, key, 1);
	if (res)
		return res;

	if (*outlen < 2 || out[0] != 0 || out[1] != 2)
		return -1;

	/* Skip PS (pseudorandom non-zero octets) */
	pos = out + 2;
	end = out + *outlen;
	while (*pos && pos < end)
		pos++;
	if (pos == end)
		return -1;
	pos++;

	*outlen -= pos - out;

	/* Strip PKCS #1 header */
	os_memmove(out, pos, *outlen);

	return 0;
}


int pkcs1_decrypt_public_key(struct crypto_rsa_key *key,
			     const u8 *crypt, size_t crypt_len,
			     u8 *plain, size_t *plain_len)
{
	size_t len;
	u8 *pos;

	len = *plain_len;
	if (crypto_rsa_exptmod(crypt, crypt_len, plain, &len, key, 0) < 0)
		return -1;

	/*
	 * PKCS #1 v1.5, 8.1:
	 *
	 * EB = 00 || BT || PS || 00 || D
	 * BT = 00 or 01
	 * PS = k-3-||D|| times (00 if BT=00) or (FF if BT=01)
	 * k = length of modulus in octets
	 */

	if (len < 3 + 8 + 16 /* min hash len */ ||
	    plain[0] != 0x00 || (plain[1] != 0x00 && plain[1] != 0x01)) {
		wpa_printf(MSG_INFO, "LibTomCrypt: Invalid signature EB "
			   "structure");
		return -1;
	}

	pos = plain + 3;
	if (plain[1] == 0x00) {
		/* BT = 00 */
		if (plain[2] != 0x00) {
			wpa_printf(MSG_INFO, "LibTomCrypt: Invalid signature "
				   "PS (BT=00)");
			return -1;
		}
		while (pos + 1 < plain + len && *pos == 0x00 && pos[1] == 0x00)
			pos++;
	} else {
		/* BT = 01 */
		if (plain[2] != 0xff) {
			wpa_printf(MSG_INFO, "LibTomCrypt: Invalid signature "
				   "PS (BT=01)");
			return -1;
		}
		while (pos < plain + len && *pos == 0xff)
			pos++;
	}

	if (pos - plain - 2 < 8) {
		/* PKCS #1 v1.5, 8.1: At least eight octets long PS */
		wpa_printf(MSG_INFO, "LibTomCrypt: Too short signature "
			   "padding");
		return -1;
	}

	if (pos + 16 /* min hash len */ >= plain + len || *pos != 0x00) {
		wpa_printf(MSG_INFO, "LibTomCrypt: Invalid signature EB "
			   "structure (2)");
		return -1;
	}
	pos++;
	len -= pos - plain;

	/* Strip PKCS #1 header */
	os_memmove(plain, pos, len);
	*plain_len = len;

	return 0;
}
