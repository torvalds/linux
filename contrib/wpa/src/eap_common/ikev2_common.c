/*
 * IKEv2 common routines for initiator and responder
 * Copyright (c) 2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/crypto.h"
#include "crypto/md5.h"
#include "crypto/sha1.h"
#include "crypto/random.h"
#include "ikev2_common.h"


static const struct ikev2_integ_alg ikev2_integ_algs[] = {
	{ AUTH_HMAC_SHA1_96, 20, 12 },
	{ AUTH_HMAC_MD5_96, 16, 12 }
};

#define NUM_INTEG_ALGS ARRAY_SIZE(ikev2_integ_algs)


static const struct ikev2_prf_alg ikev2_prf_algs[] = {
	{ PRF_HMAC_SHA1, 20, 20 },
	{ PRF_HMAC_MD5, 16, 16 }
};

#define NUM_PRF_ALGS ARRAY_SIZE(ikev2_prf_algs)


static const struct ikev2_encr_alg ikev2_encr_algs[] = {
	{ ENCR_AES_CBC, 16, 16 }, /* only 128-bit keys supported for now */
	{ ENCR_3DES, 24, 8 }
};

#define NUM_ENCR_ALGS ARRAY_SIZE(ikev2_encr_algs)


const struct ikev2_integ_alg * ikev2_get_integ(int id)
{
	size_t i;

	for (i = 0; i < NUM_INTEG_ALGS; i++) {
		if (ikev2_integ_algs[i].id == id)
			return &ikev2_integ_algs[i];
	}

	return NULL;
}


int ikev2_integ_hash(int alg, const u8 *key, size_t key_len, const u8 *data,
		     size_t data_len, u8 *hash)
{
	u8 tmphash[IKEV2_MAX_HASH_LEN];

	switch (alg) {
	case AUTH_HMAC_SHA1_96:
		if (key_len != 20)
			return -1;
		if (hmac_sha1(key, key_len, data, data_len, tmphash) < 0)
			return -1;
		os_memcpy(hash, tmphash, 12);
		break;
	case AUTH_HMAC_MD5_96:
		if (key_len != 16)
			return -1;
		if (hmac_md5(key, key_len, data, data_len, tmphash) < 0)
			return -1;
		os_memcpy(hash, tmphash, 12);
		break;
	default:
		return -1;
	}

	return 0;
}


const struct ikev2_prf_alg * ikev2_get_prf(int id)
{
	size_t i;

	for (i = 0; i < NUM_PRF_ALGS; i++) {
		if (ikev2_prf_algs[i].id == id)
			return &ikev2_prf_algs[i];
	}

	return NULL;
}


int ikev2_prf_hash(int alg, const u8 *key, size_t key_len,
		   size_t num_elem, const u8 *addr[], const size_t *len,
		   u8 *hash)
{
	switch (alg) {
	case PRF_HMAC_SHA1:
		return hmac_sha1_vector(key, key_len, num_elem, addr, len,
					hash);
	case PRF_HMAC_MD5:
		return hmac_md5_vector(key, key_len, num_elem, addr, len, hash);
	default:
		return -1;
	}
}


int ikev2_prf_plus(int alg, const u8 *key, size_t key_len,
		   const u8 *data, size_t data_len,
		   u8 *out, size_t out_len)
{
	u8 hash[IKEV2_MAX_HASH_LEN];
	size_t hash_len;
	u8 iter, *pos, *end;
	const u8 *addr[3];
	size_t len[3];
	const struct ikev2_prf_alg *prf;
	int res;

	prf = ikev2_get_prf(alg);
	if (prf == NULL)
		return -1;
	hash_len = prf->hash_len;

	addr[0] = hash;
	len[0] = hash_len;
	addr[1] = data;
	len[1] = data_len;
	addr[2] = &iter;
	len[2] = 1;

	pos = out;
	end = out + out_len;
	iter = 1;
	while (pos < end) {
		size_t clen;
		if (iter == 1)
			res = ikev2_prf_hash(alg, key, key_len, 2, &addr[1],
					     &len[1], hash);
		else
			res = ikev2_prf_hash(alg, key, key_len, 3, addr, len,
					     hash);
		if (res < 0)
			return -1;
		clen = hash_len;
		if ((int) clen > end - pos)
			clen = end - pos;
		os_memcpy(pos, hash, clen);
		pos += clen;
		iter++;
	}

	return 0;
}


const struct ikev2_encr_alg * ikev2_get_encr(int id)
{
	size_t i;

	for (i = 0; i < NUM_ENCR_ALGS; i++) {
		if (ikev2_encr_algs[i].id == id)
			return &ikev2_encr_algs[i];
	}

	return NULL;
}


int ikev2_encr_encrypt(int alg, const u8 *key, size_t key_len, const u8 *iv,
		       const u8 *plain, u8 *crypt, size_t len)
{
	struct crypto_cipher *cipher;
	int encr_alg;

	switch (alg) {
	case ENCR_3DES:
		encr_alg = CRYPTO_CIPHER_ALG_3DES;
		break;
	case ENCR_AES_CBC:
		encr_alg = CRYPTO_CIPHER_ALG_AES;
		break;
	default:
		wpa_printf(MSG_DEBUG, "IKEV2: Unsupported encr alg %d", alg);
		return -1;
	}

	cipher = crypto_cipher_init(encr_alg, iv, key, key_len);
	if (cipher == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: Failed to initialize cipher");
		return -1;
	}

	if (crypto_cipher_encrypt(cipher, plain, crypt, len) < 0) {
		wpa_printf(MSG_INFO, "IKEV2: Encryption failed");
		crypto_cipher_deinit(cipher);
		return -1;
	}
	crypto_cipher_deinit(cipher);

	return 0;
}


int ikev2_encr_decrypt(int alg, const u8 *key, size_t key_len, const u8 *iv,
		       const u8 *crypt, u8 *plain, size_t len)
{
	struct crypto_cipher *cipher;
	int encr_alg;

	switch (alg) {
	case ENCR_3DES:
		encr_alg = CRYPTO_CIPHER_ALG_3DES;
		break;
	case ENCR_AES_CBC:
		encr_alg = CRYPTO_CIPHER_ALG_AES;
		break;
	default:
		wpa_printf(MSG_DEBUG, "IKEV2: Unsupported encr alg %d", alg);
		return -1;
	}

	cipher = crypto_cipher_init(encr_alg, iv, key, key_len);
	if (cipher == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: Failed to initialize cipher");
		return -1;
	}

	if (crypto_cipher_decrypt(cipher, crypt, plain, len) < 0) {
		wpa_printf(MSG_INFO, "IKEV2: Decryption failed");
		crypto_cipher_deinit(cipher);
		return -1;
	}
	crypto_cipher_deinit(cipher);

	return 0;
}


int ikev2_parse_payloads(struct ikev2_payloads *payloads,
			 u8 next_payload, const u8 *pos, const u8 *end)
{
	const struct ikev2_payload_hdr *phdr;

	os_memset(payloads, 0, sizeof(*payloads));

	while (next_payload != IKEV2_PAYLOAD_NO_NEXT_PAYLOAD) {
		unsigned int plen, pdatalen, left;
		const u8 *pdata;
		wpa_printf(MSG_DEBUG, "IKEV2: Processing payload %u",
			   next_payload);
		if (end < pos)
			return -1;
		left = end - pos;
		if (left < sizeof(*phdr)) {
			wpa_printf(MSG_INFO, "IKEV2:   Too short message for "
				   "payload header (left=%ld)",
				   (long) (end - pos));
			return -1;
		}
		phdr = (const struct ikev2_payload_hdr *) pos;
		plen = WPA_GET_BE16(phdr->payload_length);
		if (plen < sizeof(*phdr) || plen > left) {
			wpa_printf(MSG_INFO, "IKEV2:   Invalid payload header "
				   "length %d", plen);
			return -1;
		}

		wpa_printf(MSG_DEBUG, "IKEV2:   Next Payload: %u  Flags: 0x%x"
			   "  Payload Length: %u",
			   phdr->next_payload, phdr->flags, plen);

		pdata = (const u8 *) (phdr + 1);
		pdatalen = plen - sizeof(*phdr);

		switch (next_payload) {
		case IKEV2_PAYLOAD_SA:
			wpa_printf(MSG_DEBUG, "IKEV2:   Payload: Security "
				   "Association");
			payloads->sa = pdata;
			payloads->sa_len = pdatalen;
			break;
		case IKEV2_PAYLOAD_KEY_EXCHANGE:
			wpa_printf(MSG_DEBUG, "IKEV2:   Payload: Key "
				   "Exchange");
			payloads->ke = pdata;
			payloads->ke_len = pdatalen;
			break;
		case IKEV2_PAYLOAD_IDi:
			wpa_printf(MSG_DEBUG, "IKEV2:   Payload: IDi");
			payloads->idi = pdata;
			payloads->idi_len = pdatalen;
			break;
		case IKEV2_PAYLOAD_IDr:
			wpa_printf(MSG_DEBUG, "IKEV2:   Payload: IDr");
			payloads->idr = pdata;
			payloads->idr_len = pdatalen;
			break;
		case IKEV2_PAYLOAD_CERTIFICATE:
			wpa_printf(MSG_DEBUG, "IKEV2:   Payload: Certificate");
			payloads->cert = pdata;
			payloads->cert_len = pdatalen;
			break;
		case IKEV2_PAYLOAD_AUTHENTICATION:
			wpa_printf(MSG_DEBUG, "IKEV2:   Payload: "
				   "Authentication");
			payloads->auth = pdata;
			payloads->auth_len = pdatalen;
			break;
		case IKEV2_PAYLOAD_NONCE:
			wpa_printf(MSG_DEBUG, "IKEV2:   Payload: Nonce");
			payloads->nonce = pdata;
			payloads->nonce_len = pdatalen;
			break;
		case IKEV2_PAYLOAD_ENCRYPTED:
			wpa_printf(MSG_DEBUG, "IKEV2:   Payload: Encrypted");
			payloads->encrypted = pdata;
			payloads->encrypted_len = pdatalen;
			break;
		case IKEV2_PAYLOAD_NOTIFICATION:
			wpa_printf(MSG_DEBUG, "IKEV2:   Payload: "
				   "Notification");
			payloads->notification = pdata;
			payloads->notification_len = pdatalen;
			break;
		default:
			if (phdr->flags & IKEV2_PAYLOAD_FLAGS_CRITICAL) {
				wpa_printf(MSG_INFO, "IKEV2:   Unsupported "
					   "critical payload %u - reject the "
					   "entire message", next_payload);
				return -1;
			} else {
				wpa_printf(MSG_DEBUG, "IKEV2:   Skipped "
					   "unsupported payload %u",
					   next_payload);
			}
		}

		if (next_payload == IKEV2_PAYLOAD_ENCRYPTED &&
		    pos + plen == end) {
			/*
			 * Next Payload in the case of Encrypted Payload is
			 * actually the payload type for the first embedded
			 * payload.
			 */
			payloads->encr_next_payload = phdr->next_payload;
			next_payload = IKEV2_PAYLOAD_NO_NEXT_PAYLOAD;
		} else
			next_payload = phdr->next_payload;

		pos += plen;
	}

	if (pos != end) {
		wpa_printf(MSG_INFO, "IKEV2: Unexpected extra data after "
			   "payloads");
		return -1;
	}

	return 0;
}


int ikev2_derive_auth_data(int prf_alg, const struct wpabuf *sign_msg,
			   const u8 *ID, size_t ID_len, u8 ID_type,
			   struct ikev2_keys *keys, int initiator,
			   const u8 *shared_secret, size_t shared_secret_len,
			   const u8 *nonce, size_t nonce_len,
			   const u8 *key_pad, size_t key_pad_len,
			   u8 *auth_data)
{
	size_t sign_len, buf_len;
	u8 *sign_data, *pos, *buf, hash[IKEV2_MAX_HASH_LEN];
	const struct ikev2_prf_alg *prf;
	const u8 *SK_p = initiator ? keys->SK_pi : keys->SK_pr;

	prf = ikev2_get_prf(prf_alg);
	if (sign_msg == NULL || ID == NULL || SK_p == NULL ||
	    shared_secret == NULL || nonce == NULL || prf == NULL)
		return -1;

	/* prf(SK_pi/r,IDi/r') */
	buf_len = 4 + ID_len;
	buf = os_zalloc(buf_len);
	if (buf == NULL)
		return -1;
	buf[0] = ID_type;
	os_memcpy(buf + 4, ID, ID_len);
	if (ikev2_prf_hash(prf->id, SK_p, keys->SK_prf_len,
			   1, (const u8 **) &buf, &buf_len, hash) < 0) {
		os_free(buf);
		return -1;
	}
	os_free(buf);

	/* sign_data = msg | Nr/i | prf(SK_pi/r,IDi/r') */
	sign_len = wpabuf_len(sign_msg) + nonce_len + prf->hash_len;
	sign_data = os_malloc(sign_len);
	if (sign_data == NULL)
		return -1;
	pos = sign_data;
	os_memcpy(pos, wpabuf_head(sign_msg), wpabuf_len(sign_msg));
	pos += wpabuf_len(sign_msg);
	os_memcpy(pos, nonce, nonce_len);
	pos += nonce_len;
	os_memcpy(pos, hash, prf->hash_len);

	/* AUTH = prf(prf(Shared Secret, key pad, sign_data) */
	if (ikev2_prf_hash(prf->id, shared_secret, shared_secret_len, 1,
			   &key_pad, &key_pad_len, hash) < 0 ||
	    ikev2_prf_hash(prf->id, hash, prf->hash_len, 1,
			   (const u8 **) &sign_data, &sign_len, auth_data) < 0)
	{
		os_free(sign_data);
		return -1;
	}
	os_free(sign_data);

	return 0;
}


u8 * ikev2_decrypt_payload(int encr_id, int integ_id,
			   struct ikev2_keys *keys, int initiator,
			   const struct ikev2_hdr *hdr,
			   const u8 *encrypted, size_t encrypted_len,
			   size_t *res_len)
{
	size_t iv_len;
	const u8 *pos, *end, *iv, *integ;
	u8 hash[IKEV2_MAX_HASH_LEN], *decrypted;
	size_t decrypted_len, pad_len;
	const struct ikev2_integ_alg *integ_alg;
	const struct ikev2_encr_alg *encr_alg;
	const u8 *SK_e = initiator ? keys->SK_ei : keys->SK_er;
	const u8 *SK_a = initiator ? keys->SK_ai : keys->SK_ar;

	if (encrypted == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: No Encrypted payload in SA_AUTH");
		return NULL;
	}

	encr_alg = ikev2_get_encr(encr_id);
	if (encr_alg == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: Unsupported encryption type");
		return NULL;
	}
	iv_len = encr_alg->block_size;

	integ_alg = ikev2_get_integ(integ_id);
	if (integ_alg == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: Unsupported intergrity type");
		return NULL;
	}

	if (encrypted_len < iv_len + 1 + integ_alg->hash_len) {
		wpa_printf(MSG_INFO, "IKEV2: No room for IV or Integrity "
			  "Checksum");
		return NULL;
	}

	iv = encrypted;
	pos = iv + iv_len;
	end = encrypted + encrypted_len;
	integ = end - integ_alg->hash_len;

	if (SK_a == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: No SK_a available");
		return NULL;
	}
	if (ikev2_integ_hash(integ_id, SK_a, keys->SK_integ_len,
			     (const u8 *) hdr,
			     integ - (const u8 *) hdr, hash) < 0) {
		wpa_printf(MSG_INFO, "IKEV2: Failed to calculate integrity "
			   "hash");
		return NULL;
	}
	if (os_memcmp_const(integ, hash, integ_alg->hash_len) != 0) {
		wpa_printf(MSG_INFO, "IKEV2: Incorrect Integrity Checksum "
			   "Data");
		return NULL;
	}

	if (SK_e == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: No SK_e available");
		return NULL;
	}

	decrypted_len = integ - pos;
	decrypted = os_malloc(decrypted_len);
	if (decrypted == NULL)
		return NULL;

	if (ikev2_encr_decrypt(encr_alg->id, SK_e, keys->SK_encr_len, iv, pos,
			       decrypted, decrypted_len) < 0) {
		os_free(decrypted);
		return NULL;
	}

	pad_len = decrypted[decrypted_len - 1];
	if (decrypted_len < pad_len + 1) {
		wpa_printf(MSG_INFO, "IKEV2: Invalid padding in encrypted "
			   "payload");
		os_free(decrypted);
		return NULL;
	}

	decrypted_len -= pad_len + 1;

	*res_len = decrypted_len;
	return decrypted;
}


void ikev2_update_hdr(struct wpabuf *msg)
{
	struct ikev2_hdr *hdr;

	/* Update lenth field in HDR */
	hdr = wpabuf_mhead(msg);
	WPA_PUT_BE32(hdr->length, wpabuf_len(msg));
}


int ikev2_build_encrypted(int encr_id, int integ_id, struct ikev2_keys *keys,
			  int initiator, struct wpabuf *msg,
			  struct wpabuf *plain, u8 next_payload)
{
	struct ikev2_payload_hdr *phdr;
	size_t plen;
	size_t iv_len, pad_len;
	u8 *icv, *iv;
	const struct ikev2_integ_alg *integ_alg;
	const struct ikev2_encr_alg *encr_alg;
	const u8 *SK_e = initiator ? keys->SK_ei : keys->SK_er;
	const u8 *SK_a = initiator ? keys->SK_ai : keys->SK_ar;

	wpa_printf(MSG_DEBUG, "IKEV2: Adding Encrypted payload");

	/* Encr - RFC 4306, Sect. 3.14 */

	encr_alg = ikev2_get_encr(encr_id);
	if (encr_alg == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: Unsupported encryption type");
		return -1;
	}
	iv_len = encr_alg->block_size;

	integ_alg = ikev2_get_integ(integ_id);
	if (integ_alg == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: Unsupported intergrity type");
		return -1;
	}

	if (SK_e == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: No SK_e available");
		return -1;
	}

	if (SK_a == NULL) {
		wpa_printf(MSG_INFO, "IKEV2: No SK_a available");
		return -1;
	}

	phdr = wpabuf_put(msg, sizeof(*phdr));
	phdr->next_payload = next_payload;
	phdr->flags = 0;

	iv = wpabuf_put(msg, iv_len);
	if (random_get_bytes(iv, iv_len)) {
		wpa_printf(MSG_INFO, "IKEV2: Could not generate IV");
		return -1;
	}

	pad_len = iv_len - (wpabuf_len(plain) + 1) % iv_len;
	if (pad_len == iv_len)
		pad_len = 0;
	wpabuf_put(plain, pad_len);
	wpabuf_put_u8(plain, pad_len);

	if (ikev2_encr_encrypt(encr_alg->id, SK_e, keys->SK_encr_len, iv,
			       wpabuf_head(plain), wpabuf_mhead(plain),
			       wpabuf_len(plain)) < 0)
		return -1;

	wpabuf_put_buf(msg, plain);

	/* Need to update all headers (Length fields) prior to hash func */
	icv = wpabuf_put(msg, integ_alg->hash_len);
	plen = (u8 *) wpabuf_put(msg, 0) - (u8 *) phdr;
	WPA_PUT_BE16(phdr->payload_length, plen);

	ikev2_update_hdr(msg);

	return ikev2_integ_hash(integ_id, SK_a, keys->SK_integ_len,
				wpabuf_head(msg),
				wpabuf_len(msg) - integ_alg->hash_len, icv);

	return 0;
}


int ikev2_keys_set(struct ikev2_keys *keys)
{
	return keys->SK_d && keys->SK_ai && keys->SK_ar && keys->SK_ei &&
		keys->SK_er && keys->SK_pi && keys->SK_pr;
}


void ikev2_free_keys(struct ikev2_keys *keys)
{
	os_free(keys->SK_d);
	os_free(keys->SK_ai);
	os_free(keys->SK_ar);
	os_free(keys->SK_ei);
	os_free(keys->SK_er);
	os_free(keys->SK_pi);
	os_free(keys->SK_pr);
	keys->SK_d = keys->SK_ai = keys->SK_ar = keys->SK_ei = keys->SK_er =
		keys->SK_pi = keys->SK_pr = NULL;
}


int ikev2_derive_sk_keys(const struct ikev2_prf_alg *prf,
			 const struct ikev2_integ_alg *integ,
			 const struct ikev2_encr_alg *encr,
			 const u8 *skeyseed, const u8 *data, size_t data_len,
			 struct ikev2_keys *keys)
{
	u8 *keybuf, *pos;
	size_t keybuf_len;

	/*
	 * {SK_d | SK_ai | SK_ar | SK_ei | SK_er | SK_pi | SK_pr } =
	 *	prf+(SKEYSEED, Ni | Nr | SPIi | SPIr )
	 */
	ikev2_free_keys(keys);
	keys->SK_d_len = prf->key_len;
	keys->SK_integ_len = integ->key_len;
	keys->SK_encr_len = encr->key_len;
	keys->SK_prf_len = prf->key_len;

	keybuf_len = keys->SK_d_len + 2 * keys->SK_integ_len +
		2 * keys->SK_encr_len + 2 * keys->SK_prf_len;
	keybuf = os_malloc(keybuf_len);
	if (keybuf == NULL)
		return -1;

	if (ikev2_prf_plus(prf->id, skeyseed, prf->hash_len,
			   data, data_len, keybuf, keybuf_len)) {
		os_free(keybuf);
		return -1;
	}

	pos = keybuf;

	keys->SK_d = os_malloc(keys->SK_d_len);
	if (keys->SK_d) {
		os_memcpy(keys->SK_d, pos, keys->SK_d_len);
		wpa_hexdump_key(MSG_DEBUG, "IKEV2: SK_d",
				keys->SK_d, keys->SK_d_len);
	}
	pos += keys->SK_d_len;

	keys->SK_ai = os_malloc(keys->SK_integ_len);
	if (keys->SK_ai) {
		os_memcpy(keys->SK_ai, pos, keys->SK_integ_len);
		wpa_hexdump_key(MSG_DEBUG, "IKEV2: SK_ai",
				keys->SK_ai, keys->SK_integ_len);
	}
	pos += keys->SK_integ_len;

	keys->SK_ar = os_malloc(keys->SK_integ_len);
	if (keys->SK_ar) {
		os_memcpy(keys->SK_ar, pos, keys->SK_integ_len);
		wpa_hexdump_key(MSG_DEBUG, "IKEV2: SK_ar",
				keys->SK_ar, keys->SK_integ_len);
	}
	pos += keys->SK_integ_len;

	keys->SK_ei = os_malloc(keys->SK_encr_len);
	if (keys->SK_ei) {
		os_memcpy(keys->SK_ei, pos, keys->SK_encr_len);
		wpa_hexdump_key(MSG_DEBUG, "IKEV2: SK_ei",
				keys->SK_ei, keys->SK_encr_len);
	}
	pos += keys->SK_encr_len;

	keys->SK_er = os_malloc(keys->SK_encr_len);
	if (keys->SK_er) {
		os_memcpy(keys->SK_er, pos, keys->SK_encr_len);
		wpa_hexdump_key(MSG_DEBUG, "IKEV2: SK_er",
				keys->SK_er, keys->SK_encr_len);
	}
	pos += keys->SK_encr_len;

	keys->SK_pi = os_malloc(keys->SK_prf_len);
	if (keys->SK_pi) {
		os_memcpy(keys->SK_pi, pos, keys->SK_prf_len);
		wpa_hexdump_key(MSG_DEBUG, "IKEV2: SK_pi",
				keys->SK_pi, keys->SK_prf_len);
	}
	pos += keys->SK_prf_len;

	keys->SK_pr = os_malloc(keys->SK_prf_len);
	if (keys->SK_pr) {
		os_memcpy(keys->SK_pr, pos, keys->SK_prf_len);
		wpa_hexdump_key(MSG_DEBUG, "IKEV2: SK_pr",
				keys->SK_pr, keys->SK_prf_len);
	}

	os_free(keybuf);

	if (!ikev2_keys_set(keys)) {
		ikev2_free_keys(keys);
		return -1;
	}

	return 0;
}
