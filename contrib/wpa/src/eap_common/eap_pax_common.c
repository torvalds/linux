/*
 * EAP server/peer: EAP-PAX shared routines
 * Copyright (c) 2005, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/sha1.h"
#include "eap_pax_common.h"


/**
 * eap_pax_kdf - PAX Key Derivation Function
 * @mac_id: MAC ID (EAP_PAX_MAC_*) / currently, only HMAC_SHA1_128 is supported
 * @key: Secret key (X)
 * @key_len: Length of the secret key in bytes
 * @identifier: Public identifier for the key (Y)
 * @entropy: Exchanged entropy to seed the KDF (Z)
 * @entropy_len: Length of the entropy in bytes
 * @output_len: Output len in bytes (W)
 * @output: Buffer for the derived key
 * Returns: 0 on success, -1 failed
 *
 * RFC 4746, Section 2.6: PAX-KDF-W(X, Y, Z)
 */
int eap_pax_kdf(u8 mac_id, const u8 *key, size_t key_len,
		const char *identifier,
		const u8 *entropy, size_t entropy_len,
		size_t output_len, u8 *output)
{
	u8 mac[SHA1_MAC_LEN];
	u8 counter, *pos;
	const u8 *addr[3];
	size_t len[3];
	size_t num_blocks, left;

	num_blocks = (output_len + EAP_PAX_MAC_LEN - 1) / EAP_PAX_MAC_LEN;
	if (identifier == NULL || num_blocks >= 255)
		return -1;

	/* TODO: add support for EAP_PAX_HMAC_SHA256_128 */
	if (mac_id != EAP_PAX_MAC_HMAC_SHA1_128)
		return -1;

	addr[0] = (const u8 *) identifier;
	len[0] = os_strlen(identifier);
	addr[1] = entropy;
	len[1] = entropy_len;
	addr[2] = &counter;
	len[2] = 1;

	pos = output;
	left = output_len;
	for (counter = 1; counter <= (u8) num_blocks; counter++) {
		size_t clen = left > EAP_PAX_MAC_LEN ? EAP_PAX_MAC_LEN : left;
		if (hmac_sha1_vector(key, key_len, 3, addr, len, mac) < 0)
			return -1;
		os_memcpy(pos, mac, clen);
		pos += clen;
		left -= clen;
	}

	return 0;
}


/**
 * eap_pax_mac - EAP-PAX MAC
 * @mac_id: MAC ID (EAP_PAX_MAC_*) / currently, only HMAC_SHA1_128 is supported
 * @key: Secret key
 * @key_len: Length of the secret key in bytes
 * @data1: Optional data, first block; %NULL if not used
 * @data1_len: Length of data1 in bytes
 * @data2: Optional data, second block; %NULL if not used
 * @data2_len: Length of data2 in bytes
 * @data3: Optional data, third block; %NULL if not used
 * @data3_len: Length of data3 in bytes
 * @mac: Buffer for the MAC value (EAP_PAX_MAC_LEN = 16 bytes)
 * Returns: 0 on success, -1 on failure
 *
 * Wrapper function to calculate EAP-PAX MAC.
 */
int eap_pax_mac(u8 mac_id, const u8 *key, size_t key_len,
		const u8 *data1, size_t data1_len,
		const u8 *data2, size_t data2_len,
		const u8 *data3, size_t data3_len,
		u8 *mac)
{
	u8 hash[SHA1_MAC_LEN];
	const u8 *addr[3];
	size_t len[3];
	size_t count;

	/* TODO: add support for EAP_PAX_HMAC_SHA256_128 */
	if (mac_id != EAP_PAX_MAC_HMAC_SHA1_128)
		return -1;

	addr[0] = data1;
	len[0] = data1_len;
	addr[1] = data2;
	len[1] = data2_len;
	addr[2] = data3;
	len[2] = data3_len;

	count = (data1 ? 1 : 0) + (data2 ? 1 : 0) + (data3 ? 1 : 0);
	if (hmac_sha1_vector(key, key_len, count, addr, len, hash) < 0)
		return -1;
	os_memcpy(mac, hash, EAP_PAX_MAC_LEN);

	return 0;
}


/**
 * eap_pax_initial_key_derivation - EAP-PAX initial key derivation
 * @mac_id: MAC ID (EAP_PAX_MAC_*) / currently, only HMAC_SHA1_128 is supported
 * @ak: Authentication Key
 * @e: Entropy
 * @mk: Buffer for the derived Master Key
 * @ck: Buffer for the derived Confirmation Key
 * @ick: Buffer for the derived Integrity Check Key
 * @mid: Buffer for the derived Method ID
 * Returns: 0 on success, -1 on failure
 */
int eap_pax_initial_key_derivation(u8 mac_id, const u8 *ak, const u8 *e,
				   u8 *mk, u8 *ck, u8 *ick, u8 *mid)
{
	wpa_printf(MSG_DEBUG, "EAP-PAX: initial key derivation");
	if (eap_pax_kdf(mac_id, ak, EAP_PAX_AK_LEN, "Master Key",
			e, 2 * EAP_PAX_RAND_LEN, EAP_PAX_MK_LEN, mk) ||
	    eap_pax_kdf(mac_id, mk, EAP_PAX_MK_LEN, "Confirmation Key",
			e, 2 * EAP_PAX_RAND_LEN, EAP_PAX_CK_LEN, ck) ||
	    eap_pax_kdf(mac_id, mk, EAP_PAX_MK_LEN, "Integrity Check Key",
			e, 2 * EAP_PAX_RAND_LEN, EAP_PAX_ICK_LEN, ick) ||
	    eap_pax_kdf(mac_id, mk, EAP_PAX_MK_LEN, "Method ID",
			e, 2 * EAP_PAX_RAND_LEN, EAP_PAX_MID_LEN, mid))
		return -1;

	wpa_hexdump_key(MSG_MSGDUMP, "EAP-PAX: AK", ak, EAP_PAX_AK_LEN);
	wpa_hexdump_key(MSG_MSGDUMP, "EAP-PAX: MK", mk, EAP_PAX_MK_LEN);
	wpa_hexdump_key(MSG_MSGDUMP, "EAP-PAX: CK", ck, EAP_PAX_CK_LEN);
	wpa_hexdump_key(MSG_MSGDUMP, "EAP-PAX: ICK", ick, EAP_PAX_ICK_LEN);
	wpa_hexdump_key(MSG_MSGDUMP, "EAP-PAX: MID", mid, EAP_PAX_MID_LEN);

	return 0;
}
