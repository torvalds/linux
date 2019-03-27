/*
 * EAP-IKEv2 common routines
 * Copyright (c) 2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "eap_defs.h"
#include "eap_common.h"
#include "ikev2_common.h"
#include "eap_ikev2_common.h"


int eap_ikev2_derive_keymat(int prf, struct ikev2_keys *keys,
			    const u8 *i_nonce, size_t i_nonce_len,
			    const u8 *r_nonce, size_t r_nonce_len,
			    u8 *keymat)
{
	u8 *nonces;
	size_t nlen;

	/* KEYMAT = prf+(SK_d, Ni | Nr) */
	if (keys->SK_d == NULL || i_nonce == NULL || r_nonce == NULL)
		return -1;

	nlen = i_nonce_len + r_nonce_len;
	nonces = os_malloc(nlen);
	if (nonces == NULL)
		return -1;
	os_memcpy(nonces, i_nonce, i_nonce_len);
	os_memcpy(nonces + i_nonce_len, r_nonce, r_nonce_len);

	if (ikev2_prf_plus(prf, keys->SK_d, keys->SK_d_len, nonces, nlen,
			   keymat, EAP_MSK_LEN + EAP_EMSK_LEN)) {
		os_free(nonces);
		return -1;
	}
	os_free(nonces);

	wpa_hexdump_key(MSG_DEBUG, "EAP-IKEV2: KEYMAT",
			keymat, EAP_MSK_LEN + EAP_EMSK_LEN);

	return 0;
}


struct wpabuf * eap_ikev2_build_frag_ack(u8 id, u8 code)
{
	struct wpabuf *msg;

	msg = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_IKEV2, 0, code, id);
	if (msg == NULL) {
		wpa_printf(MSG_ERROR, "EAP-IKEV2: Failed to allocate memory "
			   "for fragment ack");
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "EAP-IKEV2: Send fragment ack");

	return msg;
}


int eap_ikev2_validate_icv(int integ_alg, struct ikev2_keys *keys,
			   int initiator, const struct wpabuf *msg,
			   const u8 *pos, const u8 *end)
{
	const struct ikev2_integ_alg *integ;
	size_t icv_len;
	u8 icv[IKEV2_MAX_HASH_LEN];
	const u8 *SK_a = initiator ? keys->SK_ai : keys->SK_ar;

	integ = ikev2_get_integ(integ_alg);
	if (integ == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Unknown INTEG "
			   "transform / cannot validate ICV");
		return -1;
	}
	icv_len = integ->hash_len;

	if (end - pos < (int) icv_len) {
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: Not enough room in the "
			   "message for Integrity Checksum Data");
		return -1;
	}

	if (SK_a == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-IKEV2: No SK_a for ICV validation");
		return -1;
	}

	if (ikev2_integ_hash(integ_alg, SK_a, keys->SK_integ_len,
			     wpabuf_head(msg),
			     wpabuf_len(msg) - icv_len, icv) < 0) {
		wpa_printf(MSG_INFO, "EAP-IKEV2: Could not calculate ICV");
		return -1;
	}

	if (os_memcmp_const(icv, end - icv_len, icv_len) != 0) {
		wpa_printf(MSG_INFO, "EAP-IKEV2: Invalid ICV");
		wpa_hexdump(MSG_DEBUG, "EAP-IKEV2: Calculated ICV",
			    icv, icv_len);
		wpa_hexdump(MSG_DEBUG, "EAP-IKEV2: Received ICV",
			    end - icv_len, icv_len);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "EAP-IKEV2: Valid Integrity Checksum Data in "
		   "the received message");

	return icv_len;
}
