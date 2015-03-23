/*
 * hostapd / EAP-PEAP (draft-josefsson-pppext-eap-tls-eap-10.txt)
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
#include "crypto/random.h"
#include "eap_i.h"
#include "eap_tls_common.h"
#include "eap_common/eap_tlv_common.h"
#include "eap_common/eap_peap_common.h"
#include "tncs.h"


/* Maximum supported PEAP version
 * 0 = Microsoft's PEAP version 0; draft-kamath-pppext-peapv0-00.txt
 * 1 = draft-josefsson-ppext-eap-tls-eap-05.txt
 * 2 = draft-josefsson-ppext-eap-tls-eap-10.txt
 */
#define EAP_PEAP_VERSION 1


static void eap_peap_reset(struct eap_sm *sm, void *priv);


struct eap_peap_data {
	struct eap_ssl_data ssl;
	enum {
		START, PHASE1, PHASE1_ID2, PHASE2_START, PHASE2_ID,
		PHASE2_METHOD, PHASE2_SOH,
		PHASE2_TLV, SUCCESS_REQ, FAILURE_REQ, SUCCESS, FAILURE
	} state;

	int peap_version;
	int recv_version;
	const struct eap_method *phase2_method;
	void *phase2_priv;
	int force_version;
	struct wpabuf *pending_phase2_resp;
	enum { TLV_REQ_NONE, TLV_REQ_SUCCESS, TLV_REQ_FAILURE } tlv_request;
	int crypto_binding_sent;
	int crypto_binding_used;
	enum { NO_BINDING, OPTIONAL_BINDING, REQUIRE_BINDING } crypto_binding;
	u8 binding_nonce[32];
	u8 ipmk[40];
	u8 cmk[20];
	u8 *phase2_key;
	size_t phase2_key_len;
	struct wpabuf *soh_response;
};


static const char * eap_peap_state_txt(int state)
{
	switch (state) {
	case START:
		return "START";
	case PHASE1:
		return "PHASE1";
	case PHASE1_ID2:
		return "PHASE1_ID2";
	case PHASE2_START:
		return "PHASE2_START";
	case PHASE2_ID:
		return "PHASE2_ID";
	case PHASE2_METHOD:
		return "PHASE2_METHOD";
	case PHASE2_SOH:
		return "PHASE2_SOH";
	case PHASE2_TLV:
		return "PHASE2_TLV";
	case SUCCESS_REQ:
		return "SUCCESS_REQ";
	case FAILURE_REQ:
		return "FAILURE_REQ";
	case SUCCESS:
		return "SUCCESS";
	case FAILURE:
		return "FAILURE";
	default:
		return "Unknown?!";
	}
}


static void eap_peap_state(struct eap_peap_data *data, int state)
{
	wpa_printf(MSG_DEBUG, "EAP-PEAP: %s -> %s",
		   eap_peap_state_txt(data->state),
		   eap_peap_state_txt(state));
	data->state = state;
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


static void eap_peap_req_success(struct eap_sm *sm,
				 struct eap_peap_data *data)
{
	if (data->state == FAILURE || data->state == FAILURE_REQ) {
		eap_peap_state(data, FAILURE);
		return;
	}

	if (data->peap_version == 0) {
		data->tlv_request = TLV_REQ_SUCCESS;
		eap_peap_state(data, PHASE2_TLV);
	} else {
		eap_peap_state(data, SUCCESS_REQ);
	}
}


static void eap_peap_req_failure(struct eap_sm *sm,
				 struct eap_peap_data *data)
{
	if (data->state == FAILURE || data->state == FAILURE_REQ ||
	    data->state == SUCCESS_REQ || data->tlv_request != TLV_REQ_NONE) {
		eap_peap_state(data, FAILURE);
		return;
	}

	if (data->peap_version == 0) {
		data->tlv_request = TLV_REQ_FAILURE;
		eap_peap_state(data, PHASE2_TLV);
	} else {
		eap_peap_state(data, FAILURE_REQ);
	}
}


static void * eap_peap_init(struct eap_sm *sm)
{
	struct eap_peap_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->peap_version = EAP_PEAP_VERSION;
	data->force_version = -1;
	if (sm->user && sm->user->force_version >= 0) {
		data->force_version = sm->user->force_version;
		wpa_printf(MSG_DEBUG, "EAP-PEAP: forcing version %d",
			   data->force_version);
		data->peap_version = data->force_version;
	}
	data->state = START;
	data->crypto_binding = OPTIONAL_BINDING;

	if (eap_server_tls_ssl_init(sm, &data->ssl, 0)) {
		wpa_printf(MSG_INFO, "EAP-PEAP: Failed to initialize SSL.");
		eap_peap_reset(sm, data);
		return NULL;
	}

	return data;
}


static void eap_peap_reset(struct eap_sm *sm, void *priv)
{
	struct eap_peap_data *data = priv;
	if (data == NULL)
		return;
	if (data->phase2_priv && data->phase2_method)
		data->phase2_method->reset(sm, data->phase2_priv);
	eap_server_tls_ssl_deinit(sm, &data->ssl);
	wpabuf_free(data->pending_phase2_resp);
	os_free(data->phase2_key);
	wpabuf_free(data->soh_response);
	os_free(data);
}


static struct wpabuf * eap_peap_build_start(struct eap_sm *sm,
					    struct eap_peap_data *data, u8 id)
{
	struct wpabuf *req;

	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PEAP, 1,
			    EAP_CODE_REQUEST, id);
	if (req == NULL) {
		wpa_printf(MSG_ERROR, "EAP-PEAP: Failed to allocate memory for"
			   " request");
		eap_peap_state(data, FAILURE);
		return NULL;
	}

	wpabuf_put_u8(req, EAP_TLS_FLAGS_START | data->peap_version);

	eap_peap_state(data, PHASE1);

	return req;
}


static struct wpabuf * eap_peap_build_phase2_req(struct eap_sm *sm,
						 struct eap_peap_data *data,
						 u8 id)
{
	struct wpabuf *buf, *encr_req, msgbuf;
	const u8 *req;
	size_t req_len;

	if (data->phase2_method == NULL || data->phase2_priv == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase 2 method not ready");
		return NULL;
	}
	buf = data->phase2_method->buildReq(sm, data->phase2_priv, id);
	if (data->peap_version >= 2 && buf)
		buf = eap_peapv2_tlv_eap_payload(buf);
	if (buf == NULL)
		return NULL;

	req = wpabuf_head(buf);
	req_len = wpabuf_len(buf);
	wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: Encrypting Phase 2 data",
			req, req_len);

	if (data->peap_version == 0 &&
	    data->phase2_method->method != EAP_TYPE_TLV) {
		req += sizeof(struct eap_hdr);
		req_len -= sizeof(struct eap_hdr);
	}

	wpabuf_set(&msgbuf, req, req_len);
	encr_req = eap_server_tls_encrypt(sm, &data->ssl, &msgbuf);
	wpabuf_free(buf);

	return encr_req;
}


#ifdef EAP_SERVER_TNC
static struct wpabuf * eap_peap_build_phase2_soh(struct eap_sm *sm,
						 struct eap_peap_data *data,
						 u8 id)
{
	struct wpabuf *buf1, *buf, *encr_req, msgbuf;
	const u8 *req;
	size_t req_len;

	buf1 = tncs_build_soh_request();
	if (buf1 == NULL)
		return NULL;

	buf = eap_msg_alloc(EAP_VENDOR_MICROSOFT, 0x21, wpabuf_len(buf1),
			    EAP_CODE_REQUEST, id);
	if (buf == NULL) {
		wpabuf_free(buf1);
		return NULL;
	}
	wpabuf_put_buf(buf, buf1);
	wpabuf_free(buf1);

	req = wpabuf_head(buf);
	req_len = wpabuf_len(buf);

	wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: Encrypting Phase 2 SOH data",
			req, req_len);

	req += sizeof(struct eap_hdr);
	req_len -= sizeof(struct eap_hdr);
	wpabuf_set(&msgbuf, req, req_len);

	encr_req = eap_server_tls_encrypt(sm, &data->ssl, &msgbuf);
	wpabuf_free(buf);

	return encr_req;
}
#endif /* EAP_SERVER_TNC */


static void eap_peap_get_isk(struct eap_peap_data *data,
			     u8 *isk, size_t isk_len)
{
	size_t key_len;

	os_memset(isk, 0, isk_len);
	if (data->phase2_key == NULL)
		return;

	key_len = data->phase2_key_len;
	if (key_len > isk_len)
		key_len = isk_len;
	os_memcpy(isk, data->phase2_key, key_len);
}


static int eap_peap_derive_cmk(struct eap_sm *sm, struct eap_peap_data *data)
{
	u8 *tk;
	u8 isk[32], imck[60];

	/*
	 * Tunnel key (TK) is the first 60 octets of the key generated by
	 * phase 1 of PEAP (based on TLS).
	 */
	tk = eap_server_tls_derive_key(sm, &data->ssl, "client EAP encryption",
				       EAP_TLS_KEY_LEN);
	if (tk == NULL)
		return -1;
	wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: TK", tk, 60);

	eap_peap_get_isk(data, isk, sizeof(isk));
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

	os_free(tk);

	/* TODO: fast-connect: IPMK|CMK = TK */
	os_memcpy(data->ipmk, imck, 40);
	wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: IPMK (S-IPMKj)", data->ipmk, 40);
	os_memcpy(data->cmk, imck + 40, 20);
	wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: CMK (CMKj)", data->cmk, 20);

	return 0;
}


static struct wpabuf * eap_peap_build_phase2_tlv(struct eap_sm *sm,
						 struct eap_peap_data *data,
						 u8 id)
{
	struct wpabuf *buf, *encr_req;
	size_t mlen;

	mlen = 6; /* Result TLV */
	if (data->crypto_binding != NO_BINDING)
		mlen += 60; /* Cryptobinding TLV */
#ifdef EAP_SERVER_TNC
	if (data->soh_response)
		mlen += wpabuf_len(data->soh_response);
#endif /* EAP_SERVER_TNC */

	buf = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_TLV, mlen,
			    EAP_CODE_REQUEST, id);
	if (buf == NULL)
		return NULL;

	wpabuf_put_u8(buf, 0x80); /* Mandatory */
	wpabuf_put_u8(buf, EAP_TLV_RESULT_TLV);
	/* Length */
	wpabuf_put_be16(buf, 2);
	/* Status */
	wpabuf_put_be16(buf, data->tlv_request == TLV_REQ_SUCCESS ?
			EAP_TLV_RESULT_SUCCESS : EAP_TLV_RESULT_FAILURE);

	if (data->peap_version == 0 && data->tlv_request == TLV_REQ_SUCCESS &&
	    data->crypto_binding != NO_BINDING) {
		u8 *mac;
		u8 eap_type = EAP_TYPE_PEAP;
		const u8 *addr[2];
		size_t len[2];
		u16 tlv_type;

#ifdef EAP_SERVER_TNC
		if (data->soh_response) {
			wpa_printf(MSG_DEBUG, "EAP-PEAP: Adding MS-SOH "
				   "Response TLV");
			wpabuf_put_buf(buf, data->soh_response);
			wpabuf_free(data->soh_response);
			data->soh_response = NULL;
		}
#endif /* EAP_SERVER_TNC */

		if (eap_peap_derive_cmk(sm, data) < 0 ||
		    random_get_bytes(data->binding_nonce, 32)) {
			wpabuf_free(buf);
			return NULL;
		}

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
		wpabuf_put_u8(buf, data->recv_version); /* RecvVersion */
		wpabuf_put_u8(buf, 0); /* SubType: 0 = Request, 1 = Response */
		wpabuf_put_data(buf, data->binding_nonce, 32); /* Nonce */
		mac = wpabuf_put(buf, 20); /* Compound_MAC */
		wpa_hexdump(MSG_MSGDUMP, "EAP-PEAP: Compound_MAC CMK",
			    data->cmk, 20);
		wpa_hexdump(MSG_MSGDUMP, "EAP-PEAP: Compound_MAC data 1",
			    addr[0], len[0]);
		wpa_hexdump(MSG_MSGDUMP, "EAP-PEAP: Compound_MAC data 2",
			    addr[1], len[1]);
		hmac_sha1_vector(data->cmk, 20, 2, addr, len, mac);
		wpa_hexdump(MSG_MSGDUMP, "EAP-PEAP: Compound_MAC",
			    mac, SHA1_MAC_LEN);
		data->crypto_binding_sent = 1;
	}

	wpa_hexdump_buf_key(MSG_DEBUG, "EAP-PEAP: Encrypting Phase 2 TLV data",
			    buf);

	encr_req = eap_server_tls_encrypt(sm, &data->ssl, buf);
	wpabuf_free(buf);

	return encr_req;
}


static struct wpabuf * eap_peap_build_phase2_term(struct eap_sm *sm,
						  struct eap_peap_data *data,
						  u8 id, int success)
{
	struct wpabuf *encr_req, msgbuf;
	size_t req_len;
	struct eap_hdr *hdr;

	req_len = sizeof(*hdr);
	hdr = os_zalloc(req_len);
	if (hdr == NULL)
		return NULL;

	hdr->code = success ? EAP_CODE_SUCCESS : EAP_CODE_FAILURE;
	hdr->identifier = id;
	hdr->length = host_to_be16(req_len);

	wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: Encrypting Phase 2 data",
			(u8 *) hdr, req_len);

	wpabuf_set(&msgbuf, hdr, req_len);
	encr_req = eap_server_tls_encrypt(sm, &data->ssl, &msgbuf);
	os_free(hdr);

	return encr_req;
}


static struct wpabuf * eap_peap_buildReq(struct eap_sm *sm, void *priv, u8 id)
{
	struct eap_peap_data *data = priv;

	if (data->ssl.state == FRAG_ACK) {
		return eap_server_tls_build_ack(id, EAP_TYPE_PEAP,
						data->peap_version);
	}

	if (data->ssl.state == WAIT_FRAG_ACK) {
		return eap_server_tls_build_msg(&data->ssl, EAP_TYPE_PEAP,
						data->peap_version, id);
	}

	switch (data->state) {
	case START:
		return eap_peap_build_start(sm, data, id);
	case PHASE1:
	case PHASE1_ID2:
		if (data->peap_version < 2 &&
		    tls_connection_established(sm->ssl_ctx, data->ssl.conn)) {
			wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase1 done, "
				   "starting Phase2");
			eap_peap_state(data, PHASE2_START);
		}
		break;
	case PHASE2_ID:
	case PHASE2_METHOD:
		wpabuf_free(data->ssl.tls_out);
		data->ssl.tls_out_pos = 0;
		data->ssl.tls_out = eap_peap_build_phase2_req(sm, data, id);
		break;
#ifdef EAP_SERVER_TNC
	case PHASE2_SOH:
		wpabuf_free(data->ssl.tls_out);
		data->ssl.tls_out_pos = 0;
		data->ssl.tls_out = eap_peap_build_phase2_soh(sm, data, id);
		break;
#endif /* EAP_SERVER_TNC */
	case PHASE2_TLV:
		wpabuf_free(data->ssl.tls_out);
		data->ssl.tls_out_pos = 0;
		data->ssl.tls_out = eap_peap_build_phase2_tlv(sm, data, id);
		break;
	case SUCCESS_REQ:
		wpabuf_free(data->ssl.tls_out);
		data->ssl.tls_out_pos = 0;
		data->ssl.tls_out = eap_peap_build_phase2_term(sm, data, id,
							       1);
		break;
	case FAILURE_REQ:
		wpabuf_free(data->ssl.tls_out);
		data->ssl.tls_out_pos = 0;
		data->ssl.tls_out = eap_peap_build_phase2_term(sm, data, id,
							       0);
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-PEAP: %s - unexpected state %d",
			   __func__, data->state);
		return NULL;
	}

	return eap_server_tls_build_msg(&data->ssl, EAP_TYPE_PEAP,
					data->peap_version, id);
}


static Boolean eap_peap_check(struct eap_sm *sm, void *priv,
			      struct wpabuf *respData)
{
	const u8 *pos;
	size_t len;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_PEAP, respData, &len);
	if (pos == NULL || len < 1) {
		wpa_printf(MSG_INFO, "EAP-PEAP: Invalid frame");
		return TRUE;
	}

	return FALSE;
}


static int eap_peap_phase2_init(struct eap_sm *sm, struct eap_peap_data *data,
				EapType eap_type)
{
	if (data->phase2_priv && data->phase2_method) {
		data->phase2_method->reset(sm, data->phase2_priv);
		data->phase2_method = NULL;
		data->phase2_priv = NULL;
	}
	data->phase2_method = eap_server_get_eap_method(EAP_VENDOR_IETF,
							eap_type);
	if (!data->phase2_method)
		return -1;

	sm->init_phase2 = 1;
	data->phase2_priv = data->phase2_method->init(sm);
	sm->init_phase2 = 0;
	return 0;
}


static int eap_tlv_validate_cryptobinding(struct eap_sm *sm,
					  struct eap_peap_data *data,
					  const u8 *crypto_tlv,
					  size_t crypto_tlv_len)
{
	u8 buf[61], mac[SHA1_MAC_LEN];
	const u8 *pos;

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

	if (pos[3] != 1) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Unexpected Cryptobinding TLV "
			   "SubType %d", pos[3]);
		return -1;
	}
	pos += 4;
	pos += 32; /* Nonce */

	/* Compound_MAC: HMAC-SHA1-160(cryptobinding TLV | EAP type) */
	os_memcpy(buf, crypto_tlv, 60);
	os_memset(buf + 4 + 4 + 32, 0, 20); /* Compound_MAC */
	buf[60] = EAP_TYPE_PEAP;
	hmac_sha1(data->cmk, 20, buf, sizeof(buf), mac);

	if (os_memcmp(mac, pos, SHA1_MAC_LEN) != 0) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Invalid Compound_MAC in "
			   "cryptobinding TLV");
		wpa_hexdump_key(MSG_DEBUG, "EAP-PEAP: CMK", data->cmk, 20);
		wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Cryptobinding seed data",
			    buf, 61);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "EAP-PEAP: Valid cryptobinding TLV received");

	return 0;
}


static void eap_peap_process_phase2_tlv(struct eap_sm *sm,
					struct eap_peap_data *data,
					struct wpabuf *in_data)
{
	const u8 *pos;
	size_t left;
	const u8 *result_tlv = NULL, *crypto_tlv = NULL;
	size_t result_tlv_len = 0, crypto_tlv_len = 0;
	int tlv_type, mandatory, tlv_len;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_TLV, in_data, &left);
	if (pos == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Invalid EAP-TLV header");
		return;
	}

	/* Parse TLVs */
	wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Received TLVs", pos, left);
	while (left >= 4) {
		mandatory = !!(pos[0] & 0x80);
		tlv_type = pos[0] & 0x3f;
		tlv_type = (tlv_type << 8) | pos[1];
		tlv_len = ((int) pos[2] << 8) | pos[3];
		pos += 4;
		left -= 4;
		if ((size_t) tlv_len > left) {
			wpa_printf(MSG_DEBUG, "EAP-PEAP: TLV underrun "
				   "(tlv_len=%d left=%lu)", tlv_len,
				   (unsigned long) left);
			eap_peap_state(data, FAILURE);
			return;
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
			wpa_printf(MSG_DEBUG, "EAP-PEAP: Unsupported TLV Type "
				   "%d%s", tlv_type,
				   mandatory ? " (mandatory)" : "");
			if (mandatory) {
				eap_peap_state(data, FAILURE);
				return;
			}
			/* Ignore this TLV, but process other TLVs */
			break;
		}

		pos += tlv_len;
		left -= tlv_len;
	}
	if (left) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Last TLV too short in "
			   "Request (left=%lu)", (unsigned long) left);
		eap_peap_state(data, FAILURE);
		return;
	}

	/* Process supported TLVs */
	if (crypto_tlv && data->crypto_binding_sent) {
		wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Cryptobinding TLV",
			    crypto_tlv, crypto_tlv_len);
		if (eap_tlv_validate_cryptobinding(sm, data, crypto_tlv - 4,
						   crypto_tlv_len + 4) < 0) {
			eap_peap_state(data, FAILURE);
			return;
		}
		data->crypto_binding_used = 1;
	} else if (!crypto_tlv && data->crypto_binding_sent &&
		   data->crypto_binding == REQUIRE_BINDING) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: No cryptobinding TLV");
		eap_peap_state(data, FAILURE);
		return;
	}

	if (result_tlv) {
		int status;
		const char *requested;

		wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Result TLV",
			    result_tlv, result_tlv_len);
		if (result_tlv_len < 2) {
			wpa_printf(MSG_INFO, "EAP-PEAP: Too short Result TLV "
				   "(len=%lu)",
				   (unsigned long) result_tlv_len);
			eap_peap_state(data, FAILURE);
			return;
		}
		requested = data->tlv_request == TLV_REQ_SUCCESS ? "Success" :
			"Failure";
		status = WPA_GET_BE16(result_tlv);
		if (status == EAP_TLV_RESULT_SUCCESS) {
			wpa_printf(MSG_INFO, "EAP-PEAP: TLV Result - Success "
				   "- requested %s", requested);
			if (data->tlv_request == TLV_REQ_SUCCESS)
				eap_peap_state(data, SUCCESS);
			else
				eap_peap_state(data, FAILURE);
			
		} else if (status == EAP_TLV_RESULT_FAILURE) {
			wpa_printf(MSG_INFO, "EAP-PEAP: TLV Result - Failure "
				   "- requested %s", requested);
			eap_peap_state(data, FAILURE);
		} else {
			wpa_printf(MSG_INFO, "EAP-PEAP: Unknown TLV Result "
				   "Status %d", status);
			eap_peap_state(data, FAILURE);
		}
	}
}


#ifdef EAP_SERVER_TNC
static void eap_peap_process_phase2_soh(struct eap_sm *sm,
					struct eap_peap_data *data,
					struct wpabuf *in_data)
{
	const u8 *pos, *vpos;
	size_t left;
	const u8 *soh_tlv = NULL;
	size_t soh_tlv_len = 0;
	int tlv_type, mandatory, tlv_len, vtlv_len;
	u8 next_type;
	u32 vendor_id;

	pos = eap_hdr_validate(EAP_VENDOR_MICROSOFT, 0x21, in_data, &left);
	if (pos == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Not a valid SoH EAP "
			   "Extensions Method header - skip TNC");
		goto auth_method;
	}

	/* Parse TLVs */
	wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Received TLVs (SoH)", pos, left);
	while (left >= 4) {
		mandatory = !!(pos[0] & 0x80);
		tlv_type = pos[0] & 0x3f;
		tlv_type = (tlv_type << 8) | pos[1];
		tlv_len = ((int) pos[2] << 8) | pos[3];
		pos += 4;
		left -= 4;
		if ((size_t) tlv_len > left) {
			wpa_printf(MSG_DEBUG, "EAP-PEAP: TLV underrun "
				   "(tlv_len=%d left=%lu)", tlv_len,
				   (unsigned long) left);
			eap_peap_state(data, FAILURE);
			return;
		}
		switch (tlv_type) {
		case EAP_TLV_VENDOR_SPECIFIC_TLV:
			if (tlv_len < 4) {
				wpa_printf(MSG_DEBUG, "EAP-PEAP: Too short "
					   "vendor specific TLV (len=%d)",
					   (int) tlv_len);
				eap_peap_state(data, FAILURE);
				return;
			}

			vendor_id = WPA_GET_BE32(pos);
			if (vendor_id != EAP_VENDOR_MICROSOFT) {
				if (mandatory) {
					eap_peap_state(data, FAILURE);
					return;
				}
				break;
			}

			vpos = pos + 4;
			mandatory = !!(vpos[0] & 0x80);
			tlv_type = vpos[0] & 0x3f;
			tlv_type = (tlv_type << 8) | vpos[1];
			vtlv_len = ((int) vpos[2] << 8) | vpos[3];
			vpos += 4;
			if (vpos + vtlv_len > pos + left) {
				wpa_printf(MSG_DEBUG, "EAP-PEAP: Vendor TLV "
					   "underrun");
				eap_peap_state(data, FAILURE);
				return;
			}

			if (tlv_type == 1) {
				soh_tlv = vpos;
				soh_tlv_len = vtlv_len;
				break;
			}

			wpa_printf(MSG_DEBUG, "EAP-PEAP: Unsupported MS-TLV "
				   "Type %d%s", tlv_type,
				   mandatory ? " (mandatory)" : "");
			if (mandatory) {
				eap_peap_state(data, FAILURE);
				return;
			}
			/* Ignore this TLV, but process other TLVs */
			break;
		default:
			wpa_printf(MSG_DEBUG, "EAP-PEAP: Unsupported TLV Type "
				   "%d%s", tlv_type,
				   mandatory ? " (mandatory)" : "");
			if (mandatory) {
				eap_peap_state(data, FAILURE);
				return;
			}
			/* Ignore this TLV, but process other TLVs */
			break;
		}

		pos += tlv_len;
		left -= tlv_len;
	}
	if (left) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Last TLV too short in "
			   "Request (left=%lu)", (unsigned long) left);
		eap_peap_state(data, FAILURE);
		return;
	}

	/* Process supported TLVs */
	if (soh_tlv) {
		int failure = 0;
		wpabuf_free(data->soh_response);
		data->soh_response = tncs_process_soh(soh_tlv, soh_tlv_len,
						      &failure);
		if (failure) {
			eap_peap_state(data, FAILURE);
			return;
		}
	} else {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: No SoH TLV received");
		eap_peap_state(data, FAILURE);
		return;
	}

auth_method:
	eap_peap_state(data, PHASE2_METHOD);
	next_type = sm->user->methods[0].method;
	sm->user_eap_method_index = 1;
	wpa_printf(MSG_DEBUG, "EAP-PEAP: try EAP type %d", next_type);
	eap_peap_phase2_init(sm, data, next_type);
}
#endif /* EAP_SERVER_TNC */


static void eap_peap_process_phase2_response(struct eap_sm *sm,
					     struct eap_peap_data *data,
					     struct wpabuf *in_data)
{
	u8 next_type = EAP_TYPE_NONE;
	const struct eap_hdr *hdr;
	const u8 *pos;
	size_t left;

	if (data->state == PHASE2_TLV) {
		eap_peap_process_phase2_tlv(sm, data, in_data);
		return;
	}

#ifdef EAP_SERVER_TNC
	if (data->state == PHASE2_SOH) {
		eap_peap_process_phase2_soh(sm, data, in_data);
		return;
	}
#endif /* EAP_SERVER_TNC */

	if (data->phase2_priv == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: %s - Phase2 not "
			   "initialized?!", __func__);
		return;
	}

	hdr = wpabuf_head(in_data);
	pos = (const u8 *) (hdr + 1);

	if (wpabuf_len(in_data) > sizeof(*hdr) && *pos == EAP_TYPE_NAK) {
		left = wpabuf_len(in_data) - sizeof(*hdr);
		wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Phase2 type Nak'ed; "
			    "allowed types", pos + 1, left - 1);
		eap_sm_process_nak(sm, pos + 1, left - 1);
		if (sm->user && sm->user_eap_method_index < EAP_MAX_METHODS &&
		    sm->user->methods[sm->user_eap_method_index].method !=
		    EAP_TYPE_NONE) {
			next_type = sm->user->methods[
				sm->user_eap_method_index++].method;
			wpa_printf(MSG_DEBUG, "EAP-PEAP: try EAP type %d",
				   next_type);
		} else {
			eap_peap_req_failure(sm, data);
			next_type = EAP_TYPE_NONE;
		}
		eap_peap_phase2_init(sm, data, next_type);
		return;
	}

	if (data->phase2_method->check(sm, data->phase2_priv, in_data)) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase2 check() asked to "
			   "ignore the packet");
		return;
	}

	data->phase2_method->process(sm, data->phase2_priv, in_data);

	if (sm->method_pending == METHOD_PENDING_WAIT) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase2 method is in "
			   "pending wait state - save decrypted response");
		wpabuf_free(data->pending_phase2_resp);
		data->pending_phase2_resp = wpabuf_dup(in_data);
	}

	if (!data->phase2_method->isDone(sm, data->phase2_priv))
		return;

	if (!data->phase2_method->isSuccess(sm, data->phase2_priv)) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase2 method failed");
		eap_peap_req_failure(sm, data);
		next_type = EAP_TYPE_NONE;
		eap_peap_phase2_init(sm, data, next_type);
		return;
	}

	os_free(data->phase2_key);
	if (data->phase2_method->getKey) {
		data->phase2_key = data->phase2_method->getKey(
			sm, data->phase2_priv, &data->phase2_key_len);
		if (data->phase2_key == NULL) {
			wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase2 getKey "
				   "failed");
			eap_peap_req_failure(sm, data);
			eap_peap_phase2_init(sm, data, EAP_TYPE_NONE);
			return;
		}
	}

	switch (data->state) {
	case PHASE1_ID2:
	case PHASE2_ID:
	case PHASE2_SOH:
		if (eap_user_get(sm, sm->identity, sm->identity_len, 1) != 0) {
			wpa_hexdump_ascii(MSG_DEBUG, "EAP_PEAP: Phase2 "
					  "Identity not found in the user "
					  "database",
					  sm->identity, sm->identity_len);
			eap_peap_req_failure(sm, data);
			next_type = EAP_TYPE_NONE;
			break;
		}

#ifdef EAP_SERVER_TNC
		if (data->state != PHASE2_SOH && sm->tnc &&
		    data->peap_version == 0) {
			eap_peap_state(data, PHASE2_SOH);
			wpa_printf(MSG_DEBUG, "EAP-PEAP: Try to initialize "
				   "TNC (NAP SOH)");
			next_type = EAP_TYPE_NONE;
			break;
		}
#endif /* EAP_SERVER_TNC */

		eap_peap_state(data, PHASE2_METHOD);
		next_type = sm->user->methods[0].method;
		sm->user_eap_method_index = 1;
		wpa_printf(MSG_DEBUG, "EAP-PEAP: try EAP type %d", next_type);
		break;
	case PHASE2_METHOD:
		eap_peap_req_success(sm, data);
		next_type = EAP_TYPE_NONE;
		break;
	case FAILURE:
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-PEAP: %s - unexpected state %d",
			   __func__, data->state);
		break;
	}

	eap_peap_phase2_init(sm, data, next_type);
}


static void eap_peap_process_phase2(struct eap_sm *sm,
				    struct eap_peap_data *data,
				    const struct wpabuf *respData,
				    struct wpabuf *in_buf)
{
	struct wpabuf *in_decrypted;
	const struct eap_hdr *hdr;
	size_t len;

	wpa_printf(MSG_DEBUG, "EAP-PEAP: received %lu bytes encrypted data for"
		   " Phase 2", (unsigned long) wpabuf_len(in_buf));

	if (data->pending_phase2_resp) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Pending Phase 2 response - "
			   "skip decryption and use old data");
		eap_peap_process_phase2_response(sm, data,
						 data->pending_phase2_resp);
		wpabuf_free(data->pending_phase2_resp);
		data->pending_phase2_resp = NULL;
		return;
	}

	in_decrypted = tls_connection_decrypt(sm->ssl_ctx, data->ssl.conn,
					      in_buf);
	if (in_decrypted == NULL) {
		wpa_printf(MSG_INFO, "EAP-PEAP: Failed to decrypt Phase 2 "
			   "data");
		eap_peap_state(data, FAILURE);
		return;
	}

	wpa_hexdump_buf_key(MSG_DEBUG, "EAP-PEAP: Decrypted Phase 2 EAP",
			    in_decrypted);

	hdr = wpabuf_head(in_decrypted);

	if (data->peap_version == 0 && data->state != PHASE2_TLV) {
		const struct eap_hdr *resp;
		struct eap_hdr *nhdr;
		struct wpabuf *nbuf =
			wpabuf_alloc(sizeof(struct eap_hdr) +
				     wpabuf_len(in_decrypted));
		if (nbuf == NULL) {
			wpabuf_free(in_decrypted);
			return;
		}

		resp = wpabuf_head(respData);
		nhdr = wpabuf_put(nbuf, sizeof(*nhdr));
		nhdr->code = resp->code;
		nhdr->identifier = resp->identifier;
		nhdr->length = host_to_be16(sizeof(struct eap_hdr) +
					    wpabuf_len(in_decrypted));
		wpabuf_put_buf(nbuf, in_decrypted);
		wpabuf_free(in_decrypted);

		in_decrypted = nbuf;
	} else if (data->peap_version >= 2) {
		struct eap_tlv_hdr *tlv;
		struct wpabuf *nmsg;

		if (wpabuf_len(in_decrypted) < sizeof(*tlv) + sizeof(*hdr)) {
			wpa_printf(MSG_INFO, "EAP-PEAPv2: Too short Phase 2 "
				   "EAP TLV");
			wpabuf_free(in_decrypted);
			return;
		}
		tlv = wpabuf_mhead(in_decrypted);
		if ((be_to_host16(tlv->tlv_type) & EAP_TLV_TYPE_MASK) !=
		    EAP_TLV_EAP_PAYLOAD_TLV) {
			wpa_printf(MSG_INFO, "EAP-PEAPv2: Not an EAP TLV");
			wpabuf_free(in_decrypted);
			return;
		}
		if (sizeof(*tlv) + be_to_host16(tlv->length) >
		    wpabuf_len(in_decrypted)) {
			wpa_printf(MSG_INFO, "EAP-PEAPv2: Invalid EAP TLV "
				   "length");
			wpabuf_free(in_decrypted);
			return;
		}
		hdr = (struct eap_hdr *) (tlv + 1);
		if (be_to_host16(hdr->length) > be_to_host16(tlv->length)) {
			wpa_printf(MSG_INFO, "EAP-PEAPv2: No room for full "
				   "EAP packet in EAP TLV");
			wpabuf_free(in_decrypted);
			return;
		}

		nmsg = wpabuf_alloc(be_to_host16(hdr->length));
		if (nmsg == NULL) {
			wpabuf_free(in_decrypted);
			return;
		}

		wpabuf_put_data(nmsg, hdr, be_to_host16(hdr->length));
		wpabuf_free(in_decrypted);
		in_decrypted = nmsg;
	}

	hdr = wpabuf_head(in_decrypted);
	if (wpabuf_len(in_decrypted) < (int) sizeof(*hdr)) {
		wpa_printf(MSG_INFO, "EAP-PEAP: Too short Phase 2 "
			   "EAP frame (len=%lu)",
			   (unsigned long) wpabuf_len(in_decrypted));
		wpabuf_free(in_decrypted);
		eap_peap_req_failure(sm, data);
		return;
	}
	len = be_to_host16(hdr->length);
	if (len > wpabuf_len(in_decrypted)) {
		wpa_printf(MSG_INFO, "EAP-PEAP: Length mismatch in "
			   "Phase 2 EAP frame (len=%lu hdr->length=%lu)",
			   (unsigned long) wpabuf_len(in_decrypted),
			   (unsigned long) len);
		wpabuf_free(in_decrypted);
		eap_peap_req_failure(sm, data);
		return;
	}
	wpa_printf(MSG_DEBUG, "EAP-PEAP: received Phase 2: code=%d "
		   "identifier=%d length=%lu", hdr->code, hdr->identifier,
		   (unsigned long) len);
	switch (hdr->code) {
	case EAP_CODE_RESPONSE:
		eap_peap_process_phase2_response(sm, data, in_decrypted);
		break;
	case EAP_CODE_SUCCESS:
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase 2 Success");
		if (data->state == SUCCESS_REQ) {
			eap_peap_state(data, SUCCESS);
		}
		break;
	case EAP_CODE_FAILURE:
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Phase 2 Failure");
		eap_peap_state(data, FAILURE);
		break;
	default:
		wpa_printf(MSG_INFO, "EAP-PEAP: Unexpected code=%d in "
			   "Phase 2 EAP header", hdr->code);
		break;
	}

	wpabuf_free(in_decrypted);
}


static int eap_peapv2_start_phase2(struct eap_sm *sm,
				   struct eap_peap_data *data)
{
	struct wpabuf *buf, *buf2;

	wpa_printf(MSG_DEBUG, "EAP-PEAPv2: Phase1 done, include first Phase2 "
		   "payload in the same message");
	eap_peap_state(data, PHASE1_ID2);
	if (eap_peap_phase2_init(sm, data, EAP_TYPE_IDENTITY))
		return -1;

	/* TODO: which Id to use here? */
	buf = data->phase2_method->buildReq(sm, data->phase2_priv, 6);
	if (buf == NULL)
		return -1;

	buf2 = eap_peapv2_tlv_eap_payload(buf);
	if (buf2 == NULL)
		return -1;

	wpa_hexdump_buf(MSG_DEBUG, "EAP-PEAPv2: Identity Request", buf2);

	buf = tls_connection_encrypt(sm->ssl_ctx, data->ssl.conn,
				     buf2);
	wpabuf_free(buf2);

	if (buf == NULL) {
		wpa_printf(MSG_INFO, "EAP-PEAPv2: Failed to encrypt Phase 2 "
			   "data");
		return -1;
	}

	wpa_hexdump_buf(MSG_DEBUG, "EAP-PEAPv2: Encrypted Identity Request",
			buf);

	/* Append TLS data into the pending buffer after the Server Finished */
	if (wpabuf_resize(&data->ssl.tls_out, wpabuf_len(buf)) < 0) {
		wpabuf_free(buf);
		return -1;
	}
	wpabuf_put_buf(data->ssl.tls_out, buf);
	wpabuf_free(buf);

	return 0;
}


static int eap_peap_process_version(struct eap_sm *sm, void *priv,
				    int peer_version)
{
	struct eap_peap_data *data = priv;

	data->recv_version = peer_version;
	if (data->force_version >= 0 && peer_version != data->force_version) {
		wpa_printf(MSG_INFO, "EAP-PEAP: peer did not select the forced"
			   " version (forced=%d peer=%d) - reject",
			   data->force_version, peer_version);
		return -1;
	}
	if (peer_version < data->peap_version) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: peer ver=%d, own ver=%d; "
			   "use version %d",
			   peer_version, data->peap_version, peer_version);
		data->peap_version = peer_version;
	}

	return 0;
}


static void eap_peap_process_msg(struct eap_sm *sm, void *priv,
				 const struct wpabuf *respData)
{
	struct eap_peap_data *data = priv;

	switch (data->state) {
	case PHASE1:
		if (eap_server_tls_phase1(sm, &data->ssl) < 0) {
			eap_peap_state(data, FAILURE);
			break;
		}

		if (data->peap_version >= 2 &&
		    tls_connection_established(sm->ssl_ctx, data->ssl.conn)) {
			if (eap_peapv2_start_phase2(sm, data)) {
				eap_peap_state(data, FAILURE);
				break;
			}
		}
		break;
	case PHASE2_START:
		eap_peap_state(data, PHASE2_ID);
		eap_peap_phase2_init(sm, data, EAP_TYPE_IDENTITY);
		break;
	case PHASE1_ID2:
	case PHASE2_ID:
	case PHASE2_METHOD:
	case PHASE2_SOH:
	case PHASE2_TLV:
		eap_peap_process_phase2(sm, data, respData, data->ssl.tls_in);
		break;
	case SUCCESS_REQ:
		eap_peap_state(data, SUCCESS);
		break;
	case FAILURE_REQ:
		eap_peap_state(data, FAILURE);
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Unexpected state %d in %s",
			   data->state, __func__);
		break;
	}
}


static void eap_peap_process(struct eap_sm *sm, void *priv,
			     struct wpabuf *respData)
{
	struct eap_peap_data *data = priv;
	if (eap_server_tls_process(sm, &data->ssl, respData, data,
				   EAP_TYPE_PEAP, eap_peap_process_version,
				   eap_peap_process_msg) < 0)
		eap_peap_state(data, FAILURE);
}


static Boolean eap_peap_isDone(struct eap_sm *sm, void *priv)
{
	struct eap_peap_data *data = priv;
	return data->state == SUCCESS || data->state == FAILURE;
}


static u8 * eap_peap_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_peap_data *data = priv;
	u8 *eapKeyData;

	if (data->state != SUCCESS)
		return NULL;

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
		eapKeyData = os_malloc(EAP_TLS_KEY_LEN);
		if (eapKeyData) {
			os_memcpy(eapKeyData, csk, EAP_TLS_KEY_LEN);
			*len = EAP_TLS_KEY_LEN;
			wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Derived key",
				    eapKeyData, EAP_TLS_KEY_LEN);
		} else {
			wpa_printf(MSG_DEBUG, "EAP-PEAP: Failed to derive "
				   "key");
		}

		return eapKeyData;
	}

	/* TODO: PEAPv1 - different label in some cases */
	eapKeyData = eap_server_tls_derive_key(sm, &data->ssl,
					       "client EAP encryption",
					       EAP_TLS_KEY_LEN);
	if (eapKeyData) {
		*len = EAP_TLS_KEY_LEN;
		wpa_hexdump(MSG_DEBUG, "EAP-PEAP: Derived key",
			    eapKeyData, EAP_TLS_KEY_LEN);
	} else {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Failed to derive key");
	}

	return eapKeyData;
}


static Boolean eap_peap_isSuccess(struct eap_sm *sm, void *priv)
{
	struct eap_peap_data *data = priv;
	return data->state == SUCCESS;
}


int eap_server_peap_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_IETF, EAP_TYPE_PEAP, "PEAP");
	if (eap == NULL)
		return -1;

	eap->init = eap_peap_init;
	eap->reset = eap_peap_reset;
	eap->buildReq = eap_peap_buildReq;
	eap->check = eap_peap_check;
	eap->process = eap_peap_process;
	eap->isDone = eap_peap_isDone;
	eap->getKey = eap_peap_getKey;
	eap->isSuccess = eap_peap_isSuccess;

	ret = eap_server_method_register(eap);
	if (ret)
		eap_server_method_free(eap);
	return ret;
}
