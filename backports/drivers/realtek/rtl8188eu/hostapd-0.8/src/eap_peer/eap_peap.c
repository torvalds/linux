/*
 * EAP peer method: EAP-PEAP (draft-josefsson-pppext-eap-tls-eap-10.txt)
 * Copyright (c) 2004-2008, Jouni Malinen <j@w1.fi>
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
#include "crypto/sha1.h"
#include "crypto/tls.h"
#include "eap_common/eap_tlv_common.h"
#include "eap_common/eap_peap_common.h"
#include "eap_i.h"
#include "eap_tls_common.h"
#include "eap_config.h"
#include "tncc.h"


/* Maximum supported PEAP version
 * 0 = Microsoft's PEAP version 0; draft-kamath-pppext-peapv0-00.txt
 * 1 = draft-josefsson-ppext-eap-tls-eap-05.txt
 * 2 = draft-josefsson-ppext-eap-tls-eap-10.txt
 */
#define EAP_PEAP_VERSION 1


static void eap_peap_deinit(struct eap_sm *sm, void *priv);


struct eap_peap_data {
	struct eap_ssl_data ssl;

	int peap_version, force_peap_version, force_new_label;

	const struct eap_method *phase2_method;
	void *phase2_priv;
	int phase2_success;
	int phase2_eap_success;
	int phase2_eap_started;

	struct eap_method_type phase2_type;
	struct eap_method_type *phase2_types;
	size_t num_phase2_types;

	int peap_outer_success; /* 0 = PEAP terminated on Phase 2 inner
				 * EAP-Success
				 * 1 = reply with tunneled EAP-Success to inner
				 * EAP-Success and expect AS to send outer
				 * (unencrypted) EAP-Success after this
				 * 2 = reply with PEAP/TLS ACK to inner
				 * EAP-Success and expect AS to send outer
				 * (unencrypted) EAP-Success after this */
	int resuming; /* starting a resumed session */
	int reauth; /* reauthentication */
	u8 *key_data;

	struct wpabuf *pending_phase2_req;
	enum { NO_BINDING, OPTIONAL_BINDING, REQUIRE_BINDING } crypto_binding;
	int crypto_binding_used;
	u8 binding_nonce[32];
	u8 ipmk[40];
	u8 cmk[20];
	int soh; /* Whether IF-TNCCS-SOH (Statement of Health; Microsoft NAP)
		  * is enabled. */
};


static int eap_peap_parse_phase1(struct eap_peap_data *data,
				 const char *phase1)
{
	const char *pos;

	pos = os_strstr(phase1, "peapver=");
	if (pos) {
		data->force_peap_version = atoi(pos + 8);
		data->peap_version = data->force_peap_version;
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Forced PEAP version %d",
			   data->force_peap_version);
	}

	if (os_strstr(phase1, "peaplabel=1")) {
		data->force_new_label = 1;
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Force new label for key "
			   "derivation");
	}

	if (os_strstr(phase1, "peap_outer_success=0")) {
		data->peap_outer_success = 0;
		wpa_printf(MSG_DEBUG, "EAP-PEAP: terminate authentication on "
			   "tunneled EAP-Success");
	} else if (os_strstr(phase1, "peap_outer_success=1")) {
		data->peap_outer_success = 1;
		wpa_printf(MSG_DEBUG, "EAP-PEAP: send tunneled EAP-Success "
			   "after receiving tunneled EAP-Success");
	} else if (os_strstr(phase1, "peap_outer_success=2")) {
		data->peap_outer_success = 2;
		wpa_printf(MSG_DEBUG, "EAP-PEAP: send PEAP/TLS ACK after "
			   "receiving tunneled EAP-Success");
	}

	if (os_strstr(phase1, "crypto_binding=0")) {
		data->crypto_binding = NO_BINDING;
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Do not use cryptobinding");
	} else if (os_strstr(phase1, "crypto_binding=1")) {
		data->crypto_binding = OPTIONAL_BINDING;
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Optional cryptobinding");
	} else if (os_strstr(phase1, "crypto_binding=2")) {
		data->crypto_binding = REQUIRE_BINDING;
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Require cryptobinding");
	}

#ifdef EAP_TNC
	if (os_strstr(phase1, "tnc=soh2")) {
		data->soh = 2;
		wpa_printf(MSG_DEBUG, "EAP-PEAP: SoH version 2 enabled");
	} else if (os_strstr(phase1, "tnc=soh1")) {
		data->soh = 1;
		wpa_printf(MSG_DEBUG, "EAP-PEAP: SoH version 1 enabled");
	} else if (os_strstr(phase1, "tnc=soh")) {
		data->soh = 2;
		wpa_printf(MSG_DEBUG, "EAP-PEAP: SoH version 2 enabled");
	}
#endif /* EAP_TNC */

	return 0;
}


static void * eap_peap_init(struct eap_sm *sm)
{
	struct eap_peap_data *data;
	struct eap_peer_config *config = eap_get_config(sm);

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	sm->peap_done = FALSE;
	data->peap_version = EAP_PEAP_VERSION;
	data->force_peap_version = -1;
	data->peap_outer_success = 2;
	data->crypto_binding = OPTIONAL_BINDING;

	if (config && config->phase1 &&
	    eap_peap_parse_phase1(data, config->phase1) < 0) {
		eap_peap_deinit(sm, data);
		return NULL;
	}

	if (eap_peer_select_phase2_methods(config, "auth=",
					   &data->phase2_types,
					   &data->num_phase2_types) < 0) {
		eap_peap_deinit(sm, data);
		return NULL;
	}

	data->phase2_type.vendor = EAP_VENDOR_IETF;
	data->phase2_type.method = EAP_TYPE_NONE;

	if (eap_peer_tls_ssl_init(sm, &data->ssl, config)) {
		wpa_printf(MSG_INFO, "EAP-PEAP: Failed to initialize SSL.");
		eap_peap_deinit(sm, data);
		return NULL;
	}

	return data;
}


static void eap_peap_deinit(struct eap_sm *sm, void *priv)
{
	struct eap_peap_data *data = priv;
	if (data == NULL)
		return;
	if (data->phase2_priv && data->phase2_method)
		data->phase2_method->deinit(sm, data->phase2_priv);
	os_free(data->phase2_types);
	eap_peer_tls_ssl_deinit(sm, &data->ssl);
	os_free(data->key_data);
	wpabuf_free(data->pending_phase2_req);
	os_free(data);
}


/**
 * eap_tlv_build_nak - Build EAP-TLV NAK message
 * @id: EAP identifier for the header
 * @nak_type: TLV type (EAP_TLV_*)
 * Returns: Buffer to the allocated EAP-TLV NAK message or %NULL on failure
 *
 * This funtion builds an EAP-TLV NAK message. The caller is responsible for
 * freeing the returned buffer.
 */
static struct wpabuf * eap_tlv_build_nak(int id, u16 nak_type)
{
	struct wpabuf *msg;

	msg = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_TLV, 10,
			    EAP_CODE_RESPONSE, id);
	if (msg == NULL)
		return NULL;

	wpabuf_put_u8(msg, 0x80); /* Mandatory */
	wpabuf_put_u8(msg, EAP_TLV_NAK_TLV);
	wpabuf_put_be16(msg, 6); /* Length */
	wpabuf_put_be32(msg, 0); /* Vendor-Id */
	wpabuf_put_be16(msg, nak_type); /* NAK-Type */

	return msg;
}


static int eap_peap_get_isk(struct eap_sm *sm, struct eap_peap_data *data,
			    u8 *isk, size_t isk_len)
{
	u8 *key;
	size_t key_len;

	os_memset(isk, 0, isk_len);
	if (data->phase2_method == NULL || data->phase2_priv == NULL ||
	    data->phase2_method->isKeyAvailable == NULL ||
	    data->phase2_method->getKey == NULL)
		return 0;

	if (!data->phase2_method->isKeyAvailable(sm, data->phase2_priv) ||
	    (key = data->phase2_method->getKey(sm, data->phase2_priv,
					       &key_len)) == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Could not get key material "
			   "from Phase 2");
		return -1;
	}

	if (key_len > isk_len)
		key_len = isk_len;
	os_memcpy(isk, key, key_len);
	os_free(key);

	return 0;
}


static int eap_peap_derive_cmk(struct eap_sm *sm, struct eap_peap_data *data)
{
	u8 *tk;
	u8 isk[32], imck[60];

	/*
	 * Tunnel key (TK) is the first 60 octets of the key generated by
	 * phase 1 of PEAP (based on TLS).
	 */
	tk = data->key_data;
	if (tk == NULL)
		return -1;
	wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: TK", tk, 60);

	if (data->reauth &&
	    tls_connection_resumed(sm->ssl_ctx, data->ssl.conn)) {
		/* Fast-connect: IPMK|CMK = TK */
		os_memcpy(data->ipmk, tk, 40);
		wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: IPMK from TK",
				data->ipmk, 40);
		os_memcpy(data->cmk, tk + 40, 20);
		wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: CMK from TK",
				data->cmk, 20);
		return 0;
	}

	if (eap_peap_get_isk(sm, data, isk, sizeof(isk)) < 0)
		return -1;
	wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: ISK", isk, sizeof(isk));

	/*
	 * IPMK Seed = "Inner Methods Compound Keys" | ISK
	 * TempKey = First 40 octets of TK
	 * IPMK|CMK = PRF+(TempKey, IPMK Seed, 60)
	 * (note: draft-josefsson-pppext-eap-tls-eap-10.txt includes a space
	 * in the end of the label just before ISK; is that just a typo?)
	 */
	wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: TempKey", tk, 40);
	peap_prfplus(data->peap_version, tk, 40, "Inner Methods Compound Keys",
		     isk, sizeof(isk), imck, sizeof(imck));
	wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: IMCK (IPMKj)",
			imck, sizeof(imck));

	os_memcpy(data->ipmk, imck, 40);
	wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: IPMK (S-IPMKj)", data->ipmk, 40);
	os_memcpy(data->cmk, imck + 40, 20);
	wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: CMK (CMKj)", data->cmk, 20);

	return 0;
}


static int eap_tlv_add_cryptobinding(struct eap_sm *sm,
				     struct eap_peap_data *data,
				     struct wpabuf *buf)
{
	u8 *mac;
	u8 eap_type = EAP_TYPE_PEAP;
	const u8 *addr[2];
	size_t len[2];
	u16 tlv_type;

	/* Compound_MAC: HMAC-SHA1-160(cryptobinding TLV | EAP type) */
	addr[0] = wpabuf_put(buf, 0);
	len[0] = 60;
	addr[1] = &eap_type;
	len[1] = 1;

	tlv_type = EAP_TLV_CRYPTO_BINDING_TLV;
	if (data->peap_version >= 2)
		tlv_type |= EAP_TLV_TYPE_MANDATORY;
	wpabuf_put_be16(buf, tlv_type);
	wpabuf_put_be16(buf, 56);

	wpabuf_put_u8(buf, 0); /* Reserved */
	wpabuf_put_u8(buf, data->peap_version); /* Version */
	wpabuf_put_u8(buf, data->peap_version); /* RecvVersion */
	wpabuf_put_u8(buf, 1); /* SubType: 0 = Request, 1 = Response */
	wpabuf_put_data(buf, data->binding_nonce, 32); /* Nonce */
	mac = wpabuf_put(buf, 20); /* Compound_MAC */
	wpa_hexdump(MSG_MSGDUMP, "EAP-PEAP: Compound_MAC CMK", data->cmk, 20);
	wpa_hexdump(MSG_MSGDUMP, "EAP-PEAP: Compound_MAC data 1",
		    addr[0], len[0]);
	wpa_hexdump(MSG_MSGDUMP, "EAP-PEAP: Compound_MAC data 2",
		    addr[1], len[1]);
	hmac_sha1_vector(data->cmk, 20, 2, addr, len, mac);
	wpa_hexdump(MSG_MSGDUMP, "EAP-PEAP: Compound_MAC", mac, SHA1_MAC_LEN);
	data->crypto_binding_used = 1;

	return 0;
}


/**
 * eap_tlv_build_result - Build EAP-TLV Result message
 * @id: EAP identifier for the header
 * @status: Status (EAP_TLV_RESULT_SUCCESS or EAP_TLV_RESULT_FAILURE)
 * Returns: Buffer to the allocated EAP-TLV Result message or %NULL on failure
 *
 * This funtion builds an EAP-TLV Result message. The caller is responsible for
 * freeing the returned buffer.
 */
static struct wpabuf * eap_tlv_build_result(struct eap_sm *sm,
					    struct eap_peap_data *data,
					    int crypto_tlv_used,
					    int id, u16 status)
{
	struct wpabuf *msg;
	size_t len;

	if (data->crypto_binding == NO_BINDING)
		crypto_tlv_used = 0;

	len = 6;
	if (crypto_tlv_used)
		len += 60; /* Cryptobinding TLV */
	msg = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_TLV, len,
			    EAP_CODE_RESPONSE, id);
	if (msg == NULL)
		return NULL;

	wpabuf_put_u8(msg, 0x80); /* Mandatory */
	wpabuf_put_u8(msg, EAP_TLV_RESULT_TLV);
	wpabuf_put_be16(msg, 2); /* Length */
	wpabuf_put_be16(msg, status); /* Status */

	if (crypto_tlv_used && eap_tlv_add_cryptobinding(sm, data, msg)) {
		wpabuf_free(msg);
		return NULL;
	}

	return msg;
}


static int eap_tlv_validate_cryptobinding(struct eap_sm *sm,
					  struct eap_peap_data *data,
					  const u8 *crypto_tlv,
					  size_t crypto_tlv_len)
{
	u8 buf[61], mac[SHA1_MAC_LEN];
	const u8 *pos;

	if (eap_peap_derive_cmk(sm, data) < 0) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Could not derive CMK");
		return -1;
	}

	if (crypto_tlv_len != 4 + 56) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Invalid cryptobinding TLV "
			   "length %d", (int) crypto_tlv_len);
		return -1;
	}

	pos = crypto_tlv;
	pos += 4; /* TLV header */
	if (pos[1] != data->peap_version) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Cryptobinding TLV Version "
			   "mismatch (was %d; expected %d)",
			   pos[1], data->peap_version);
		return -1;
	}

	if (pos[3] != 0) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Unexpected Cryptobinding TLV "
			   "SubType %d", pos[3]);
		return -1;
	}
	pos += 4;
	os_memcpy(data->binding_nonce, pos, 32);
	pos += 32; /* Nonce */

	/* Compound_MAC: HMAC-SHA1-160(cryptobinding TLV | EAP type) */
	os_memcpy(buf, crypto_tlv, 60);
	os_memset(buf + 4 + 4 + 32, 0, 20); /* Compound_MAC */
	buf[60] = EAP_TYPE_PEAP;
	wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Compound_MAC data",
		    buf, sizeof(buf));
	hmac_sha1(data->cmk, 20, buf, sizeof(buf), mac);

	if (os_memcmp(mac, pos, SHA1_MAC_LEN) != 0) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Invalid Compound_MAC in "
			   "cryptobinding TLV");
		wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Received MAC",
			    pos, SHA1_MAC_LEN);
		wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Expected MAC",
			    mac, SHA1_MAC_LEN);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "EAP-PEAP: Valid cryptobinding TLV received");

	return 0;
}


/**
 * eap_tlv_process - Process a received EAP-TLV message and generate a response
 * @sm: Pointer to EAP state machine allocated with eap_peer_sm_init()
 * @ret: Return values from EAP request validation and processing
 * @req: EAP-TLV request to be processed. The caller must have validated that
 * the buffer is large enough to contain full request (hdr->length bytes) and
 * that the EAP type is EAP_TYPE_TLV.
 * @resp: Buffer to return a pointer to the allocated response message. This
 * field should be initialized to %NULL before the call. The value will be
 * updated if a response message is generated. The caller is responsible for
 * freeing the allocated message.
 * @force_failure: Force negotiation to fail
 * Returns: 0 on success, -1 on failure
 */
static int eap_tlv_process(struct eap_sm *sm, struct eap_peap_data *data,
			   struct eap_method_ret *ret,
			   const struct wpabuf *req, struct wpabuf **resp,
			   int force_failure)
{
	size_t left, tlv_len;
	const u8 *pos;
	const u8 *result_tlv = NULL, *crypto_tlv = NULL;
	size_t result_tlv_len = 0, crypto_tlv_len = 0;
	int tlv_type, mandatory;

	/* Parse TLVs */
	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_TLV, req, &left);
	if (pos == NULL)
		return -1;
	wpa_hexdump(MSG_DEBUG, "EAP-TLV: Received TLVs", pos, left);
	while (left >= 4) {
		mandatory = !!(pos[0] & 0x80);
		tlv_type = WPA_GET_BE16(pos) & 0x3fff;
		pos += 2;
		tlv_len = WPA_GET_BE16(pos);
		pos += 2;
		left -= 4;
		if (tlv_len > left) {
			wpa_printf(MSG_DEBUG, "EAP-TLV: TLV underrun "
				   "(tlv_len=%lu left=%lu)",
				   (unsigned long) tlv_len,
				   (unsigned long) left);
			return -1;
		}
		switch (tlv_type) {
		case EAP_TLV_RESULT_TLV:
			result_tlv = pos;
			result_tlv_len = tlv_len;
			break;
		case EAP_TLV_CRYPTO_BINDING_TLV:
			crypto_tlv = pos;
			crypto_tlv_len = tlv_len;
			break;
		default:
			wpa_printf(MSG_DEBUG, "EAP-TLV: Unsupported TLV Type "
				   "%d%s", tlv_type,
				   mandatory ? " (mandatory)" : "");
			if (mandatory) {
				/* NAK TLV and ignore all TLVs in this packet.
				 */
				*resp = eap_tlv_build_nak(eap_get_id(req),
							  tlv_type);
				return *resp == NULL ? -1 : 0;
			}
			/* Ignore this TLV, but process other TLVs */
			break;
		}

		pos += tlv_len;
		left -= tlv_len;
	}
	if (left) {
		wpa_printf(MSG_DEBUG, "EAP-TLV: Last TLV too short in "
			   "Request (left=%lu)", (unsigned long) left);
		return -1;
	}

	/* Process supported TLVs */
	if (crypto_tlv && data->crypto_binding != NO_BINDING) {
		wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Cryptobinding TLV",
			    crypto_tlv, crypto_tlv_len);
		if (eap_tlv_validate_cryptobinding(sm, data, crypto_tlv - 4,
						   crypto_tlv_len + 4) < 0) {
			if (result_tlv == NULL)
				return -1;
			force_failure = 1;
			crypto_tlv = NULL; /* do not include Cryptobinding TLV
					    * in response, if the received
					    * cryptobinding was invalid. */
		}
	} else if (!crypto_tlv && data->crypto_binding == REQUIRE_BINDING) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: No cryptobinding TLV");
		return -1;
	}

	if (result_tlv) {
		int status, resp_status;
		wpa_hexdump(MSG_DEBUG, "EAP-TLV: Result TLV",
			    result_tlv, result_tlv_len);
		if (result_tlv_len < 2) {
			wpa_printf(MSG_INFO, "EAP-TLV: Too short Result TLV "
				   "(len=%lu)",
				   (unsigned long) result_tlv_len);
			return -1;
		}
		status = WPA_GET_BE16(result_tlv);
		if (status == EAP_TLV_RESULT_SUCCESS) {
			wpa_printf(MSG_INFO, "EAP-TLV: TLV Result - Success "
				   "- EAP-TLV/Phase2 Completed");
			if (force_failure) {
				wpa_printf(MSG_INFO, "EAP-TLV: Earlier failure"
					   " - force failed Phase 2");
				resp_status = EAP_TLV_RESULT_FAILURE;
				ret->decision = DECISION_FAIL;
			} else {
				resp_status = EAP_TLV_RESULT_SUCCESS;
				ret->decision = DECISION_UNCOND_SUCC;
			}
		} else if (status == EAP_TLV_RESULT_FAILURE) {
			wpa_printf(MSG_INFO, "EAP-TLV: TLV Result - Failure");
			resp_status = EAP_TLV_RESULT_FAILURE;
			ret->decision = DECISION_FAIL;
		} else {
			wpa_printf(MSG_INFO, "EAP-TLV: Unknown TLV Result "
				   "Status %d", status);
			resp_status = EAP_TLV_RESULT_FAILURE;
			ret->decision = DECISION_FAIL;
		}
		ret->methodState = METHOD_DONE;

		*resp = eap_tlv_build_result(sm, data, crypto_tlv != NULL,
					     eap_get_id(req), resp_status);
	}

	return 0;
}


static struct wpabuf * eap_peapv2_tlv_eap_payload(struct wpabuf *buf)
{
	struct wpabuf *e;
	struct eap_tlv_hdr *tlv;

	if (buf == NULL)
		return NULL;

	/* Encapsulate EAP packet in EAP-Payload TLV */
	wpa_printf(MSG_DEBUG, "EAP-PEAPv2: Add EAP-Payload TLV");
	e = wpabuf_alloc(sizeof(*tlv) + wpabuf_len(buf));
	if (e == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-PEAPv2: Failed to allocate memory "
			   "for TLV encapsulation");
		wpabuf_free(buf);
		return NULL;
	}
	tlv = wpabuf_put(e, sizeof(*tlv));
	tlv->tlv_type = host_to_be16(EAP_TLV_TYPE_MANDATORY |
				     EAP_TLV_EAP_PAYLOAD_TLV);
	tlv->length = host_to_be16(wpabuf_len(buf));
	wpabuf_put_buf(e, buf);
	wpabuf_free(buf);
	return e;
}


static int eap_peap_phase2_request(struct eap_sm *sm,
				   struct eap_peap_data *data,
				   struct eap_method_ret *ret,
				   struct wpabuf *req,
				   struct wpabuf **resp)
{
	struct eap_hdr *hdr = wpabuf_mhead(req);
	size_t len = be_to_host16(hdr->length);
	u8 *pos;
	struct eap_method_ret iret;
	struct eap_peer_config *config = eap_get_config(sm);

	if (len <= sizeof(struct eap_hdr)) {
		wpa_printf(MSG_INFO, "EAP-PEAP: too short "
			   "Phase 2 request (len=%lu)", (unsigned long) len);
		return -1;
	}
	pos = (u8 *) (hdr + 1);
	wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase 2 Request: type=%d", *pos);
	switch (*pos) {
	case EAP_TYPE_IDENTITY:
		*resp = eap_sm_buildIdentity(sm, hdr->identifier, 1);
		break;
	case EAP_TYPE_TLV:
		os_memset(&iret, 0, sizeof(iret));
		if (eap_tlv_process(sm, data, &iret, req, resp,
				    data->phase2_eap_started &&
				    !data->phase2_eap_success)) {
			ret->methodState = METHOD_DONE;
			ret->decision = DECISION_FAIL;
			return -1;
		}
		if (iret.methodState == METHOD_DONE ||
		    iret.methodState == METHOD_MAY_CONT) {
			ret->methodState = iret.methodState;
			ret->decision = iret.decision;
			data->phase2_success = 1;
		}
		break;
	case EAP_TYPE_EXPANDED:
#ifdef EAP_TNC
		if (data->soh) {
			const u8 *epos;
			size_t eleft;

			epos = eap_hdr_validate(EAP_VENDOR_MICROSOFT, 0x21,
						req, &eleft);
			if (epos) {
				struct wpabuf *buf;
				wpa_printf(MSG_DEBUG,
					   "EAP-PEAP: SoH EAP Extensions");
				buf = tncc_process_soh_request(data->soh,
							       epos, eleft);
				if (buf) {
					*resp = eap_msg_alloc(
						EAP_VENDOR_MICROSOFT, 0x21,
						wpabuf_len(buf),
						EAP_CODE_RESPONSE,
						hdr->identifier);
					if (*resp == NULL) {
						ret->methodState = METHOD_DONE;
						ret->decision = DECISION_FAIL;
						return -1;
					}
					wpabuf_put_buf(*resp, buf);
					wpabuf_free(buf);
					break;
				}
			}
		}
#endif /* EAP_TNC */
		/* fall through */
	default:
		if (data->phase2_type.vendor == EAP_VENDOR_IETF &&
		    data->phase2_type.method == EAP_TYPE_NONE) {
			size_t i;
			for (i = 0; i < data->num_phase2_types; i++) {
				if (data->phase2_types[i].vendor !=
				    EAP_VENDOR_IETF ||
				    data->phase2_types[i].method != *pos)
					continue;

				data->phase2_type.vendor =
					data->phase2_types[i].vendor;
				data->phase2_type.method =
					data->phase2_types[i].method;
				wpa_printf(MSG_DEBUG, "EAP-PEAP: Selected "
					   "Phase 2 EAP vendor %d method %d",
					   data->phase2_type.vendor,
					   data->phase2_type.method);
				break;
			}
		}
		if (*pos != data->phase2_type.method ||
		    *pos == EAP_TYPE_NONE) {
			if (eap_peer_tls_phase2_nak(data->phase2_types,
						    data->num_phase2_types,
						    hdr, resp))
				return -1;
			return 0;
		}

		if (data->phase2_priv == NULL) {
			data->phase2_method = eap_peer_get_eap_method(
				data->phase2_type.vendor,
				data->phase2_type.method);
			if (data->phase2_method) {
				sm->init_phase2 = 1;
				data->phase2_priv =
					data->phase2_method->init(sm);
				sm->init_phase2 = 0;
			}
		}
		if (data->phase2_priv == NULL || data->phase2_method == NULL) {
			wpa_printf(MSG_INFO, "EAP-PEAP: failed to initialize "
				   "Phase 2 EAP method %d", *pos);
			ret->methodState = METHOD_DONE;
			ret->decision = DECISION_FAIL;
			return -1;
		}
		data->phase2_eap_started = 1;
		os_memset(&iret, 0, sizeof(iret));
		*resp = data->phase2_method->process(sm, data->phase2_priv,
						     &iret, req);
		if ((iret.methodState == METHOD_DONE ||
		     iret.methodState == METHOD_MAY_CONT) &&
		    (iret.decision == DECISION_UNCOND_SUCC ||
		     iret.decision == DECISION_COND_SUCC)) {
			data->phase2_eap_success = 1;
			data->phase2_success = 1;
		}
		break;
	}

	if (*resp == NULL &&
	    (config->pending_req_identity || config->pending_req_password ||
	     config->pending_req_otp || config->pending_req_new_password)) {
		wpabuf_free(data->pending_phase2_req);
		data->pending_phase2_req = wpabuf_alloc_copy(hdr, len);
	}

	return 0;
}


static int eap_peap_decrypt(struct eap_sm *sm, struct eap_peap_data *data,
			    struct eap_method_ret *ret,
			    const struct eap_hdr *req,
			    const struct wpabuf *in_data,
			    struct wpabuf **out_data)
{
	struct wpabuf *in_decrypted = NULL;
	int res, skip_change = 0;
	struct eap_hdr *hdr, *rhdr;
	struct wpabuf *resp = NULL;
	size_t len;

	wpa_printf(MSG_DEBUG, "EAP-PEAP: received %lu bytes encrypted data for"
		   " Phase 2", (unsigned long) wpabuf_len(in_data));

	if (data->pending_phase2_req) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Pending Phase 2 request - "
			   "skip decryption and use old data");
		/* Clear TLS reassembly state. */
		eap_peer_tls_reset_input(&data->ssl);
		in_decrypted = data->pending_phase2_req;
		data->pending_phase2_req = NULL;
		skip_change = 1;
		goto continue_req;
	}

	if (wpabuf_len(in_data) == 0 && sm->workaround &&
	    data->phase2_success) {
		/*
		 * Cisco ACS seems to be using TLS ACK to terminate
		 * EAP-PEAPv0/GTC. Try to reply with TLS ACK.
		 */
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Received TLS ACK, but "
			   "expected data - acknowledge with TLS ACK since "
			   "Phase 2 has been completed");
		ret->decision = DECISION_COND_SUCC;
		ret->methodState = METHOD_DONE;
		return 1;
	} else if (wpabuf_len(in_data) == 0) {
		/* Received TLS ACK - requesting more fragments */
		return eap_peer_tls_encrypt(sm, &data->ssl, EAP_TYPE_PEAP,
					    data->peap_version,
					    req->identifier, NULL, out_data);
	}

	res = eap_peer_tls_decrypt(sm, &data->ssl, in_data, &in_decrypted);
	if (res)
		return res;

continue_req:
	wpa_hexdump_buf(MSG_DEBUG, "EAP-PEAP: Decrypted Phase 2 EAP",
			in_decrypted);

	hdr = wpabuf_mhead(in_decrypted);
	if (wpabuf_len(in_decrypted) == 5 && hdr->code == EAP_CODE_REQUEST &&
	    be_to_host16(hdr->length) == 5 &&
	    eap_get_type(in_decrypted) == EAP_TYPE_IDENTITY) {
		/* At least FreeRADIUS seems to send full EAP header with
		 * EAP Request Identity */
		skip_change = 1;
	}
	if (wpabuf_len(in_decrypted) >= 5 && hdr->code == EAP_CODE_REQUEST &&
	    eap_get_type(in_decrypted) == EAP_TYPE_TLV) {
		skip_change = 1;
	}

	if (data->peap_version == 0 && !skip_change) {
		struct eap_hdr *nhdr;
		struct wpabuf *nmsg = wpabuf_alloc(sizeof(struct eap_hdr) +
						   wpabuf_len(in_decrypted));
		if (nmsg == NULL) {
			wpabuf_free(in_decrypted);
			return 0;
		}
		nhdr = wpabuf_put(nmsg, sizeof(*nhdr));
		wpabuf_put_buf(nmsg, in_decrypted);
		nhdr->code = req->code;
		nhdr->identifier = req->identifier;
		nhdr->length = host_to_be16(sizeof(struct eap_hdr) +
					    wpabuf_len(in_decrypted));

		wpabuf_free(in_decrypted);
		in_decrypted = nmsg;
	}

	if (data->peap_version >= 2) {
		struct eap_tlv_hdr *tlv;
		struct wpabuf *nmsg;

		if (wpabuf_len(in_decrypted) < sizeof(*tlv) + sizeof(*hdr)) {
			wpa_printf(MSG_INFO, "EAP-PEAPv2: Too short Phase 2 "
				   "EAP TLV");
			wpabuf_free(in_decrypted);
			return 0;
		}
		tlv = wpabuf_mhead(in_decrypted);
		if ((be_to_host16(tlv->tlv_type) & 0x3fff) !=
		    EAP_TLV_EAP_PAYLOAD_TLV) {
			wpa_printf(MSG_INFO, "EAP-PEAPv2: Not an EAP TLV");
			wpabuf_free(in_decrypted);
			return 0;
		}
		if (sizeof(*tlv) + be_to_host16(tlv->length) >
		    wpabuf_len(in_decrypted)) {
			wpa_printf(MSG_INFO, "EAP-PEAPv2: Invalid EAP TLV "
				   "length");
			wpabuf_free(in_decrypted);
			return 0;
		}
		hdr = (struct eap_hdr *) (tlv + 1);
		if (be_to_host16(hdr->length) > be_to_host16(tlv->length)) {
			wpa_printf(MSG_INFO, "EAP-PEAPv2: No room for full "
				   "EAP packet in EAP TLV");
			wpabuf_free(in_decrypted);
			return 0;
		}

		nmsg = wpabuf_alloc(be_to_host16(hdr->length));
		if (nmsg == NULL) {
			wpabuf_free(in_decrypted);
			return 0;
		}

		wpabuf_put_data(nmsg, hdr, be_to_host16(hdr->length));
		wpabuf_free(in_decrypted);
		in_decrypted = nmsg;
	}

	hdr = wpabuf_mhead(in_decrypted);
	if (wpabuf_len(in_decrypted) < sizeof(*hdr)) {
		wpa_printf(MSG_INFO, "EAP-PEAP: Too short Phase 2 "
			   "EAP frame (len=%lu)",
			   (unsigned long) wpabuf_len(in_decrypted));
		wpabuf_free(in_decrypted);
		return 0;
	}
	len = be_to_host16(hdr->length);
	if (len > wpabuf_len(in_decrypted)) {
		wpa_printf(MSG_INFO, "EAP-PEAP: Length mismatch in "
			   "Phase 2 EAP frame (len=%lu hdr->length=%lu)",
			   (unsigned long) wpabuf_len(in_decrypted),
			   (unsigned long) len);
		wpabuf_free(in_decrypted);
		return 0;
	}
	if (len < wpabuf_len(in_decrypted)) {
		wpa_printf(MSG_INFO, "EAP-PEAP: Odd.. Phase 2 EAP header has "
			   "shorter length than full decrypted data "
			   "(%lu < %lu)",
			   (unsigned long) len,
			   (unsigned long) wpabuf_len(in_decrypted));
	}
	wpa_printf(MSG_DEBUG, "EAP-PEAP: received Phase 2: code=%d "
		   "identifier=%d length=%lu", hdr->code, hdr->identifier,
		   (unsigned long) len);
	switch (hdr->code) {
	case EAP_CODE_REQUEST:
		if (eap_peap_phase2_request(sm, data, ret, in_decrypted,
					    &resp)) {
			wpabuf_free(in_decrypted);
			wpa_printf(MSG_INFO, "EAP-PEAP: Phase2 Request "
				   "processing failed");
			return 0;
		}
		break;
	case EAP_CODE_SUCCESS:
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase 2 Success");
		if (data->peap_version == 1) {
			/* EAP-Success within TLS tunnel is used to indicate
			 * shutdown of the TLS channel. The authentication has
			 * been completed. */
			if (data->phase2_eap_started &&
			    !data->phase2_eap_success) {
				wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase 2 "
					   "Success used to indicate success, "
					   "but Phase 2 EAP was not yet "
					   "completed successfully");
				ret->methodState = METHOD_DONE;
				ret->decision = DECISION_FAIL;
				wpabuf_free(in_decrypted);
				return 0;
			}
			wpa_printf(MSG_DEBUG, "EAP-PEAP: Version 1 - "
				   "EAP-Success within TLS tunnel - "
				   "authentication completed");
			ret->decision = DECISION_UNCOND_SUCC;
			ret->methodState = METHOD_DONE;
			data->phase2_success = 1;
			if (data->peap_outer_success == 2) {
				wpabuf_free(in_decrypted);
				wpa_printf(MSG_DEBUG, "EAP-PEAP: Use TLS ACK "
					   "to finish authentication");
				return 1;
			} else if (data->peap_outer_success == 1) {
				/* Reply with EAP-Success within the TLS
				 * channel to complete the authentication. */
				resp = wpabuf_alloc(sizeof(struct eap_hdr));
				if (resp) {
					rhdr = wpabuf_put(resp, sizeof(*rhdr));
					rhdr->code = EAP_CODE_SUCCESS;
					rhdr->identifier = hdr->identifier;
					rhdr->length =
						host_to_be16(sizeof(*rhdr));
				}
			} else {
				/* No EAP-Success expected for Phase 1 (outer,
				 * unencrypted auth), so force EAP state
				 * machine to SUCCESS state. */
				sm->peap_done = TRUE;
			}
		} else {
			/* FIX: ? */
		}
		break;
	case EAP_CODE_FAILURE:
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase 2 Failure");
		ret->decision = DECISION_FAIL;
		ret->methodState = METHOD_MAY_CONT;
		ret->allowNotifications = FALSE;
		/* Reply with EAP-Failure within the TLS channel to complete
		 * failure reporting. */
		resp = wpabuf_alloc(sizeof(struct eap_hdr));
		if (resp) {
			rhdr = wpabuf_put(resp, sizeof(*rhdr));
			rhdr->code = EAP_CODE_FAILURE;
			rhdr->identifier = hdr->identifier;
			rhdr->length = host_to_be16(sizeof(*rhdr));
		}
		break;
	default:
		wpa_printf(MSG_INFO, "EAP-PEAP: Unexpected code=%d in "
			   "Phase 2 EAP header", hdr->code);
		break;
	}

	wpabuf_free(in_decrypted);

	if (resp) {
		int skip_change2 = 0;
		struct wpabuf *rmsg, buf;

		wpa_hexdump_buf_key(MSG_DEBUG,
				    "EAP-PEAP: Encrypting Phase 2 data", resp);
		/* PEAP version changes */
		if (data->peap_version >= 2) {
			resp = eap_peapv2_tlv_eap_payload(resp);
			if (resp == NULL)
				return -1;
		}
		if (wpabuf_len(resp) >= 5 &&
		    wpabuf_head_u8(resp)[0] == EAP_CODE_RESPONSE &&
		    eap_get_type(resp) == EAP_TYPE_TLV)
			skip_change2 = 1;
		rmsg = resp;
		if (data->peap_version == 0 && !skip_change2) {
			wpabuf_set(&buf, wpabuf_head_u8(resp) +
				   sizeof(struct eap_hdr),
				   wpabuf_len(resp) - sizeof(struct eap_hdr));
			rmsg = &buf;
		}

		if (eap_peer_tls_encrypt(sm, &data->ssl, EAP_TYPE_PEAP,
					 data->peap_version, req->identifier,
					 rmsg, out_data)) {
			wpa_printf(MSG_INFO, "EAP-PEAP: Failed to encrypt "
				   "a Phase 2 frame");
		}
		wpabuf_free(resp);
	}

	return 0;
}


static struct wpabuf * eap_peap_process(struct eap_sm *sm, void *priv,
					struct eap_method_ret *ret,
					const struct wpabuf *reqData)
{
	const struct eap_hdr *req;
	size_t left;
	int res;
	u8 flags, id;
	struct wpabuf *resp;
	const u8 *pos;
	struct eap_peap_data *data = priv;

	pos = eap_peer_tls_process_init(sm, &data->ssl, EAP_TYPE_PEAP, ret,
					reqData, &left, &flags);
	if (pos == NULL)
		return NULL;
	req = wpabuf_head(reqData);
	id = req->identifier;

	if (flags & EAP_TLS_FLAGS_START) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Start (server ver=%d, own "
			   "ver=%d)", flags & EAP_TLS_VERSION_MASK,
			data->peap_version);
		if ((flags & EAP_TLS_VERSION_MASK) < data->peap_version)
			data->peap_version = flags & EAP_TLS_VERSION_MASK;
		if (data->force_peap_version >= 0 &&
		    data->force_peap_version != data->peap_version) {
			wpa_printf(MSG_WARNING, "EAP-PEAP: Failed to select "
				   "forced PEAP version %d",
				   data->force_peap_version);
			ret->methodState = METHOD_DONE;
			ret->decision = DECISION_FAIL;
			ret->allowNotifications = FALSE;
			return NULL;
		}
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Using PEAP version %d",
			   data->peap_version);
		left = 0; /* make sure that this frame is empty, even though it
			   * should always be, anyway */
	}

	resp = NULL;
	if (tls_connection_established(sm->ssl_ctx, data->ssl.conn) &&
	    !data->resuming) {
		struct wpabuf msg;
		wpabuf_set(&msg, pos, left);
		res = eap_peap_decrypt(sm, data, ret, req, &msg, &resp);
	} else {
		res = eap_peer_tls_process_helper(sm, &data->ssl,
						  EAP_TYPE_PEAP,
						  data->peap_version, id, pos,
						  left, &resp);

		if (tls_connection_established(sm->ssl_ctx, data->ssl.conn)) {
			char *label;
			wpa_printf(MSG_DEBUG,
				   "EAP-PEAP: TLS done, proceed to Phase 2");
			os_free(data->key_data);
			/* draft-josefsson-ppext-eap-tls-eap-05.txt
			 * specifies that PEAPv1 would use "client PEAP
			 * encryption" as the label. However, most existing
			 * PEAPv1 implementations seem to be using the old
			 * label, "client EAP encryption", instead. Use the old
			 * label by default, but allow it to be configured with
			 * phase1 parameter peaplabel=1. */
			if (data->peap_version > 1 || data->force_new_label)
				label = "client PEAP encryption";
			else
				label = "client EAP encryption";
			wpa_printf(MSG_DEBUG, "EAP-PEAP: using label '%s' in "
				   "key derivation", label);
			data->key_data =
				eap_peer_tls_derive_key(sm, &data->ssl, label,
							EAP_TLS_KEY_LEN);
			if (data->key_data) {
				wpa_hexdump_key(MSG_DEBUG, 
						"EAP-PEAP: Derived key",
						data->key_data,
						EAP_TLS_KEY_LEN);
			} else {
				wpa_printf(MSG_DEBUG, "EAP-PEAP: Failed to "
					   "derive key");
			}

			if (sm->workaround && data->resuming) {
				/*
				 * At least few RADIUS servers (Aegis v1.1.6;
				 * but not v1.1.4; and Cisco ACS) seem to be
				 * terminating PEAPv1 (Aegis) or PEAPv0 (Cisco
				 * ACS) session resumption with outer
				 * EAP-Success. This does not seem to follow
				 * draft-josefsson-pppext-eap-tls-eap-05.txt
				 * section 4.2, so only allow this if EAP
				 * workarounds are enabled.
				 */
				wpa_printf(MSG_DEBUG, "EAP-PEAP: Workaround - "
					   "allow outer EAP-Success to "
					   "terminate PEAP resumption");
				ret->decision = DECISION_COND_SUCC;
				data->phase2_success = 1;
			}

			data->resuming = 0;
		}

		if (res == 2) {
			struct wpabuf msg;
			/*
			 * Application data included in the handshake message.
			 */
			wpabuf_free(data->pending_phase2_req);
			data->pending_phase2_req = resp;
			resp = NULL;
			wpabuf_set(&msg, pos, left);
			res = eap_peap_decrypt(sm, data, ret, req, &msg,
					       &resp);
		}
	}

	if (ret->methodState == METHOD_DONE) {
		ret->allowNotifications = FALSE;
	}

	if (res == 1) {
		wpabuf_free(resp);
		return eap_peer_tls_build_ack(id, EAP_TYPE_PEAP,
					      data->peap_version);
	}

	return resp;
}


static Boolean eap_peap_has_reauth_data(struct eap_sm *sm, void *priv)
{
	struct eap_peap_data *data = priv;
	return tls_connection_established(sm->ssl_ctx, data->ssl.conn) &&
		data->phase2_success;
}


static void eap_peap_deinit_for_reauth(struct eap_sm *sm, void *priv)
{
	struct eap_peap_data *data = priv;
	wpabuf_free(data->pending_phase2_req);
	data->pending_phase2_req = NULL;
	data->crypto_binding_used = 0;
}


static void * eap_peap_init_for_reauth(struct eap_sm *sm, void *priv)
{
	struct eap_peap_data *data = priv;
	os_free(data->key_data);
	data->key_data = NULL;
	if (eap_peer_tls_reauth_init(sm, &data->ssl)) {
		os_free(data);
		return NULL;
	}
	if (data->phase2_priv && data->phase2_method &&
	    data->phase2_method->init_for_reauth)
		data->phase2_method->init_for_reauth(sm, data->phase2_priv);
	data->phase2_success = 0;
	data->phase2_eap_success = 0;
	data->phase2_eap_started = 0;
	data->resuming = 1;
	data->reauth = 1;
	sm->peap_done = FALSE;
	return priv;
}


static int eap_peap_get_status(struct eap_sm *sm, void *priv, char *buf,
			       size_t buflen, int verbose)
{
	struct eap_peap_data *data = priv;
	int len, ret;

	len = eap_peer_tls_status(sm, &data->ssl, buf, buflen, verbose);
	if (data->phase2_method) {
		ret = os_snprintf(buf + len, buflen - len,
				  "EAP-PEAPv%d Phase2 method=%s\n",
				  data->peap_version,
				  data->phase2_method->name);
		if (ret < 0 || (size_t) ret >= buflen - len)
			return len;
		len += ret;
	}
	return len;
}


static Boolean eap_peap_isKeyAvailable(struct eap_sm *sm, void *priv)
{
	struct eap_peap_data *data = priv;
	return data->key_data != NULL && data->phase2_success;
}


static u8 * eap_peap_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_peap_data *data = priv;
	u8 *key;

	if (data->key_data == NULL || !data->phase2_success)
		return NULL;

	key = os_malloc(EAP_TLS_KEY_LEN);
	if (key == NULL)
		return NULL;

	*len = EAP_TLS_KEY_LEN;

	if (data->crypto_binding_used) {
		u8 csk[128];
		/*
		 * Note: It looks like Microsoft implementation requires null
		 * termination for this label while the one used for deriving
		 * IPMK|CMK did not use null termination.
		 */
		peap_prfplus(data->peap_version, data->ipmk, 40,
			     "Session Key Generating Function",
			     (u8 *) "\00", 1, csk, sizeof(csk));
		wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: CSK", csk, sizeof(csk));
		os_memcpy(key, csk, EAP_TLS_KEY_LEN);
		wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Derived key",
			    key, EAP_TLS_KEY_LEN);
	} else
		os_memcpy(key, data->key_data, EAP_TLS_KEY_LEN);

	return key;
}


int eap_peer_peap_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_PEAP, "PEAP");
	if (eap == NULL)
		return -1;

	eap->init = eap_peap_init;
	eap->deinit = eap_peap_deinit;
	eap->process = eap_peap_process;
	eap->isKeyAvailable = eap_peap_isKeyAvailable;
	eap->getKey = eap_peap_getKey;
	eap->get_status = eap_peap_get_status;
	eap->has_reauth_data = eap_peap_has_reauth_data;
	eap->deinit_for_reauth = eap_peap_deinit_for_reauth;
	eap->init_for_reauth = eap_peap_init_for_reauth;

	ret = eap_peer_method_register(eap);
	if (ret)
		eap_peer_method_free(eap);
	return ret;
}
