/*
 * EAP server/peer: EAP-GPSK shared routines
 * Copyright (c) 2006-2007, Jouni Malinen <j@w1.fi>
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
#include "crypto/aes_wrap.h"
#include "crypto/sha256.h"
#include "eap_defs.h"
#include "eap_gpsk_common.h"


/**
 * eap_gpsk_supported_ciphersuite - Check whether ciphersuite is supported
 * @vendor: CSuite/Vendor
 * @specifier: CSuite/Specifier
 * Returns: 1 if ciphersuite is support, or 0 if not
 */
int eap_gpsk_supported_ciphersuite(int vendor, int specifier)
{
	if (vendor == EAP_GPSK_VENDOR_IETF &&
	    specifier == EAP_GPSK_CIPHER_AES)
		return 1;
#ifdef EAP_GPSK_SHA256
	if (vendor == EAP_GPSK_VENDOR_IETF &&
	    specifier == EAP_GPSK_CIPHER_SHA256)
		return 1;
#endif /* EAP_GPSK_SHA256 */
	return 0;
}


static int eap_gpsk_gkdf_cmac(const u8 *psk /* Y */,
			      const u8 *data /* Z */, size_t data_len,
			      u8 *buf, size_t len /* X */)
{
	u8 *opos;
	size_t i, n, hashlen, left, clen;
	u8 ibuf[2], hash[16];
	const u8 *addr[2];
	size_t vlen[2];

	hashlen = sizeof(hash);
	/* M_i = MAC_Y (i || Z); (MAC = AES-CMAC-128) */
	addr[0] = ibuf;
	vlen[0] = sizeof(ibuf);
	addr[1] = data;
	vlen[1] = data_len;

	opos = buf;
	left = len;
	n = (len + hashlen - 1) / hashlen;
	for (i = 1; i <= n; i++) {
		WPA_PUT_BE16(ibuf, i);
		if (omac1_aes_128_vector(psk, 2, addr, vlen, hash))
			return -1;
		clen = left > hashlen ? hashlen : left;
		os_memcpy(opos, hash, clen);
		opos += clen;
		left -= clen;
	}

	return 0;
}


#ifdef EAP_GPSK_SHA256
static int eap_gpsk_gkdf_sha256(const u8 *psk /* Y */,
				const u8 *data /* Z */, size_t data_len,
				u8 *buf, size_t len /* X */)
{
	u8 *opos;
	size_t i, n, hashlen, left, clen;
	u8 ibuf[2], hash[SHA256_MAC_LEN];
	const u8 *addr[2];
	size_t vlen[2];

	hashlen = SHA256_MAC_LEN;
	/* M_i = MAC_Y (i || Z); (MAC = HMAC-SHA256) */
	addr[0] = ibuf;
	vlen[0] = sizeof(ibuf);
	addr[1] = data;
	vlen[1] = data_len;

	opos = buf;
	left = len;
	n = (len + hashlen - 1) / hashlen;
	for (i = 1; i <= n; i++) {
		WPA_PUT_BE16(ibuf, i);
		hmac_sha256_vector(psk, 32, 2, addr, vlen, hash);
		clen = left > hashlen ? hashlen : left;
		os_memcpy(opos, hash, clen);
		opos += clen;
		left -= clen;
	}

	return 0;
}
#endif /* EAP_GPSK_SHA256 */


static int eap_gpsk_derive_keys_helper(u32 csuite_specifier,
				       u8 *kdf_out, size_t kdf_out_len,
				       const u8 *psk, size_t psk_len,
				       const u8 *seed, size_t seed_len,
				       u8 *msk, u8 *emsk,
				       u8 *sk, size_t sk_len,
				       u8 *pk, size_t pk_len)
{
	u8 mk[32], *pos, *data;
	size_t data_len, mk_len;
	int (*gkdf)(const u8 *_psk, const u8 *_data, size_t _data_len,
		    u8 *buf, size_t len);

	gkdf = NULL;
	switch (csuite_specifier) {
	case EAP_GPSK_CIPHER_AES:
		gkdf = eap_gpsk_gkdf_cmac;
		mk_len = 16;
		break;
#ifdef EAP_GPSK_SHA256
	case EAP_GPSK_CIPHER_SHA256:
		gkdf = eap_gpsk_gkdf_sha256;
		mk_len = SHA256_MAC_LEN;
		break;
#endif /* EAP_GPSK_SHA256 */
	default:
		return -1;
	}

	if (psk_len < mk_len)
		return -1;

	data_len = 2 + psk_len + 6 + seed_len;
	data = os_malloc(data_len);
	if (data == NULL)
		return -1;
	pos = data;
	WPA_PUT_BE16(pos, psk_len);
	pos += 2;
	os_memcpy(pos, psk, psk_len);
	pos += psk_len;
	WPA_PUT_BE32(pos, EAP_GPSK_VENDOR_IETF); /* CSuite/Vendor = IETF */
	pos += 4;
	WPA_PUT_BE16(pos, csuite_specifier); /* CSuite/Specifier */
	pos += 2;
	os_memcpy(pos, seed, seed_len); /* inputString */
	wpa_hexdump_key(MSG_DEBUG, "EAP-GPSK: Data to MK derivation",
			data, data_len);

	if (gkdf(psk, data, data_len, mk, mk_len) < 0) {
		os_free(data);
		return -1;
	}
	os_free(data);
	wpa_hexdump_key(MSG_DEBUG, "EAP-GPSK: MK", mk, mk_len);

	if (gkdf(mk, seed, seed_len, kdf_out, kdf_out_len) < 0)
		return -1;

	pos = kdf_out;
	wpa_hexdump_key(MSG_DEBUG, "EAP-GPSK: MSK", pos, EAP_MSK_LEN);
	os_memcpy(msk, pos, EAP_MSK_LEN);
	pos += EAP_MSK_LEN;

	wpa_hexdump_key(MSG_DEBUG, "EAP-GPSK: EMSK", pos, EAP_EMSK_LEN);
	os_memcpy(emsk, pos, EAP_EMSK_LEN);
	pos += EAP_EMSK_LEN;

	wpa_hexdump_key(MSG_DEBUG, "EAP-GPSK: SK", pos, sk_len);
	os_memcpy(sk, pos, sk_len);
	pos += sk_len;

	if (pk) {
		wpa_hexdump_key(MSG_DEBUG, "EAP-GPSK: PK", pos, pk_len);
		os_memcpy(pk, pos, pk_len);
	}

	return 0;
}


static int eap_gpsk_derive_keys_aes(const u8 *psk, size_t psk_len,
				    const u8 *seed, size_t seed_len,
				    u8 *msk, u8 *emsk, u8 *sk, size_t *sk_len,
				    u8 *pk, size_t *pk_len)
{
#define EAP_GPSK_SK_LEN_AES 16
#define EAP_GPSK_PK_LEN_AES 16
	u8 kdf_out[EAP_MSK_LEN + EAP_EMSK_LEN + EAP_GPSK_SK_LEN_AES +
		   EAP_GPSK_PK_LEN_AES];

	/*
	 * inputString = RAND_Peer || ID_Peer || RAND_Server || ID_Server
	 *            (= seed)
	 * KS = 16, PL = psk_len, CSuite_Sel = 0x00000000 0x0001
	 * MK = GKDF-16 (PSK[0..15], PL || PSK || CSuite_Sel || inputString)
	 * MSK = GKDF-160 (MK, inputString)[0..63]
	 * EMSK = GKDF-160 (MK, inputString)[64..127]
	 * SK = GKDF-160 (MK, inputString)[128..143]
	 * PK = GKDF-160 (MK, inputString)[144..159]
	 * zero = 0x00 || 0x00 || ... || 0x00 (16 times)
	 * Method-ID = GKDF-16 (zero, "Method ID" || EAP_Method_Type ||
	 *                      CSuite_Sel || inputString)
	 */

	*sk_len = EAP_GPSK_SK_LEN_AES;
	*pk_len = EAP_GPSK_PK_LEN_AES;

	return eap_gpsk_derive_keys_helper(EAP_GPSK_CIPHER_AES,
					   kdf_out, sizeof(kdf_out),
					   psk, psk_len, seed, seed_len,
					   msk, emsk, sk, *sk_len,
					   pk, *pk_len);
}


#ifdef EAP_GPSK_SHA256
static int eap_gpsk_derive_keys_sha256(const u8 *psk, size_t psk_len,
				       const u8 *seed, size_t seed_len,
				       u8 *msk, u8 *emsk,
				       u8 *sk, size_t *sk_len)
{
#define EAP_GPSK_SK_LEN_SHA256 SHA256_MAC_LEN
#define EAP_GPSK_PK_LEN_SHA256 SHA256_MAC_LEN
	u8 kdf_out[EAP_MSK_LEN + EAP_EMSK_LEN + EAP_GPSK_SK_LEN_SHA256 +
		   EAP_GPSK_PK_LEN_SHA256];

	/*
	 * inputString = RAND_Peer || ID_Peer || RAND_Server || ID_Server
	 *            (= seed)
	 * KS = 32, PL = psk_len, CSuite_Sel = 0x00000000 0x0002
	 * MK = GKDF-32 (PSK[0..31], PL || PSK || CSuite_Sel || inputString)
	 * MSK = GKDF-160 (MK, inputString)[0..63]
	 * EMSK = GKDF-160 (MK, inputString)[64..127]
	 * SK = GKDF-160 (MK, inputString)[128..159]
	 * zero = 0x00 || 0x00 || ... || 0x00 (32 times)
	 * Method-ID = GKDF-16 (zero, "Method ID" || EAP_Method_Type ||
	 *                      CSuite_Sel || inputString)
	 */

	*sk_len = EAP_GPSK_SK_LEN_SHA256;

	return eap_gpsk_derive_keys_helper(EAP_GPSK_CIPHER_SHA256,
					   kdf_out, sizeof(kdf_out),
					   psk, psk_len, seed, seed_len,
					   msk, emsk, sk, *sk_len,
					   NULL, 0);
}
#endif /* EAP_GPSK_SHA256 */


/**
 * eap_gpsk_derive_keys - Derive EAP-GPSK keys
 * @psk: Pre-shared key
 * @psk_len: Length of psk in bytes
 * @vendor: CSuite/Vendor
 * @specifier: CSuite/Specifier
 * @rand_peer: 32-byte RAND_Peer
 * @rand_server: 32-byte RAND_Server
 * @id_peer: ID_Peer
 * @id_peer_len: Length of ID_Peer
 * @id_server: ID_Server
 * @id_server_len: Length of ID_Server
 * @msk: Buffer for 64-byte MSK
 * @emsk: Buffer for 64-byte EMSK
 * @sk: Buffer for SK (at least EAP_GPSK_MAX_SK_LEN bytes)
 * @sk_len: Buffer for returning length of SK
 * @pk: Buffer for PK (at least EAP_GPSK_MAX_PK_LEN bytes)
 * @pk_len: Buffer for returning length of PK
 * Returns: 0 on success, -1 on failure
 */
int eap_gpsk_derive_keys(const u8 *psk, size_t psk_len, int vendor,
			 int specifier,
			 const u8 *rand_peer, const u8 *rand_server,
			 const u8 *id_peer, size_t id_peer_len,
			 const u8 *id_server, size_t id_server_len,
			 u8 *msk, u8 *emsk, u8 *sk, size_t *sk_len,
			 u8 *pk, size_t *pk_len)
{
	u8 *seed, *pos;
	size_t seed_len;
	int ret;

	wpa_printf(MSG_DEBUG, "EAP-GPSK: Deriving keys (%d:%d)",
		   vendor, specifier);

	if (vendor != EAP_GPSK_VENDOR_IETF)
		return -1;

	wpa_hexdump_key(MSG_DEBUG, "EAP-GPSK: PSK", psk, psk_len);

	/* Seed = RAND_Peer || ID_Peer || RAND_Server || ID_Server */
	seed_len = 2 * EAP_GPSK_RAND_LEN + id_server_len + id_peer_len;
	seed = os_malloc(seed_len);
	if (seed == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Failed to allocate memory "
			   "for key derivation");
		return -1;
	}

	pos = seed;
	os_memcpy(pos, rand_peer, EAP_GPSK_RAND_LEN);
	pos += EAP_GPSK_RAND_LEN;
	os_memcpy(pos, id_peer, id_peer_len);
	pos += id_peer_len;
	os_memcpy(pos, rand_server, EAP_GPSK_RAND_LEN);
	pos += EAP_GPSK_RAND_LEN;
	os_memcpy(pos, id_server, id_server_len);
	pos += id_server_len;
	wpa_hexdump(MSG_DEBUG, "EAP-GPSK: Seed", seed, seed_len);

	switch (specifier) {
	case EAP_GPSK_CIPHER_AES:
		ret = eap_gpsk_derive_keys_aes(psk, psk_len, seed, seed_len,
					       msk, emsk, sk, sk_len,
					       pk, pk_len);
		break;
#ifdef EAP_GPSK_SHA256
	case EAP_GPSK_CIPHER_SHA256:
		ret = eap_gpsk_derive_keys_sha256(psk, psk_len, seed, seed_len,
						  msk, emsk, sk, sk_len);
		break;
#endif /* EAP_GPSK_SHA256 */
	default:
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Unknown cipher %d:%d used in "
			   "key derivation", vendor, specifier);
		ret = -1;
		break;
	}

	os_free(seed);

	return ret;
}


/**
 * eap_gpsk_mic_len - Get the length of the MIC
 * @vendor: CSuite/Vendor
 * @specifier: CSuite/Specifier
 * Returns: MIC length in bytes
 */
size_t eap_gpsk_mic_len(int vendor, int specifier)
{
	if (vendor != EAP_GPSK_VENDOR_IETF)
		return 0;

	switch (specifier) {
	case EAP_GPSK_CIPHER_AES:
		return 16;
#ifdef EAP_GPSK_SHA256
	case EAP_GPSK_CIPHER_SHA256:
		return 32;
#endif /* EAP_GPSK_SHA256 */
	default:
		return 0;
	}
}


static int eap_gpsk_compute_mic_aes(const u8 *sk, size_t sk_len,
				    const u8 *data, size_t len, u8 *mic)
{
	if (sk_len != 16) {
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Invalid SK length %lu for "
			   "AES-CMAC MIC", (unsigned long) sk_len);
		return -1;
	}

	return omac1_aes_128(sk, data, len, mic);
}


/**
 * eap_gpsk_compute_mic - Compute EAP-GPSK MIC for an EAP packet
 * @sk: Session key SK from eap_gpsk_derive_keys()
 * @sk_len: SK length in bytes from eap_gpsk_derive_keys()
 * @vendor: CSuite/Vendor
 * @specifier: CSuite/Specifier
 * @data: Input data to MIC
 * @len: Input data length in bytes
 * @mic: Buffer for the computed MIC, eap_gpsk_mic_len(cipher) bytes
 * Returns: 0 on success, -1 on failure
 */
int eap_gpsk_compute_mic(const u8 *sk, size_t sk_len, int vendor,
			 int specifier, const u8 *data, size_t len, u8 *mic)
{
	int ret;

	if (vendor != EAP_GPSK_VENDOR_IETF)
		return -1;

	switch (specifier) {
	case EAP_GPSK_CIPHER_AES:
		ret = eap_gpsk_compute_mic_aes(sk, sk_len, data, len, mic);
		break;
#ifdef EAP_GPSK_SHA256
	case EAP_GPSK_CIPHER_SHA256:
		hmac_sha256(sk, sk_len, data, len, mic);
		ret = 0;
		break;
#endif /* EAP_GPSK_SHA256 */
	default:
		wpa_printf(MSG_DEBUG, "EAP-GPSK: Unknown cipher %d:%d used in "
			   "MIC computation", vendor, specifier);
		ret = -1;
		break;
	}

	return ret;
}
