/*
 * EAP peer method: EAP-FAST (RFC 4851)
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
#include "crypto/tls.h"
#include "crypto/sha1.h"
#include "eap_common/eap_tlv_common.h"
#include "eap_i.h"
#include "eap_tls_common.h"
#include "eap_config.h"
#include "eap_fast_pac.h"

#ifdef EAP_FAST_DYNAMIC
#include "eap_fast_pac.c"
#endif /* EAP_FAST_DYNAMIC */

/* TODO:
 * - test session resumption and enable it if it interoperates
 * - password change (pending mschapv2 packet; replay decrypted packet)
 */


static void eap_fast_deinit(struct eap_sm *sm, void *priv);


struct eap_fast_data {
	struct eap_ssl_data ssl;

	int fast_version;

	const struct eap_method *phase2_method;
	void *phase2_priv;
	int phase2_success;

	struct eap_method_type phase2_type;
	struct eap_method_type *phase2_types;
	size_t num_phase2_types;
	int resuming; /* starting a resumed session */
	struct eap_fast_key_block_provisioning *key_block_p;
#define EAP_FAST_PROV_UNAUTH 1
#define EAP_FAST_PROV_AUTH 2
	int provisioning_allowed; /* Allowed PAC provisioning modes */
	int provisioning; /* doing PAC provisioning (not the normal auth) */
	int anon_provisioning; /* doing anonymous (unauthenticated)
				* provisioning */
	int session_ticket_used;

	u8 key_data[EAP_FAST_KEY_LEN];
	u8 emsk[EAP_EMSK_LEN];
	int success;

	struct eap_fast_pac *pac;
	struct eap_fast_pac *current_pac;
	size_t max_pac_list_len;
	int use_pac_binary_format;

	u8 simck[EAP_FAST_SIMCK_LEN];
	int simck_idx;

	struct wpabuf *pending_phase2_req;
};


static int eap_fast_session_ticket_cb(void *ctx, const u8 *ticket, size_t len,
				      const u8 *client_random,
				      const u8 *server_random,
				      u8 *master_secret)
{
	struct eap_fast_data *data = ctx;

	wpa_printf(MSG_DEBUG, "EAP-FAST: SessionTicket callback");

	if (client_random == NULL || server_random == NULL ||
	    master_secret == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: SessionTicket failed - fall "
			   "back to full TLS handshake");
		data->session_ticket_used = 0;
		if (data->provisioning_allowed) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: Try to provision a "
				   "new PAC-Key");
			data->provisioning = 1;
			data->current_pac = NULL;
		}
		return 0;
	}

	wpa_hexdump(MSG_DEBUG, "EAP-FAST: SessionTicket", ticket, len);

	if (data->current_pac == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: No PAC-Key available for "
			   "using SessionTicket");
		data->session_ticket_used = 0;
		return 0;
	}

	eap_fast_derive_master_secret(data->current_pac->pac_key,
				      server_random, client_random,
				      master_secret);

	data->session_ticket_used = 1;

	return 1;
}


static int eap_fast_parse_phase1(struct eap_fast_data *data,
				 const char *phase1)
{
	const char *pos;

	pos = os_strstr(phase1, "fast_provisioning=");
	if (pos) {
		data->provisioning_allowed = atoi(pos + 18);
		wpa_printf(MSG_DEBUG, "EAP-FAST: Automatic PAC provisioning "
			   "mode: %d", data->provisioning_allowed);
	}

	pos = os_strstr(phase1, "fast_max_pac_list_len=");
	if (pos) {
		data->max_pac_list_len = atoi(pos + 22);
		if (data->max_pac_list_len == 0)
			data->max_pac_list_len = 1;
		wpa_printf(MSG_DEBUG, "EAP-FAST: Maximum PAC list length: %lu",
			   (unsigned long) data->max_pac_list_len);
	}

	pos = os_strstr(phase1, "fast_pac_format=binary");
	if (pos) {
		data->use_pac_binary_format = 1;
		wpa_printf(MSG_DEBUG, "EAP-FAST: Using binary format for PAC "
			   "list");
	}

	return 0;
}


static void * eap_fast_init(struct eap_sm *sm)
{
	struct eap_fast_data *data;
	struct eap_peer_config *config = eap_get_config(sm);

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->fast_version = EAP_FAST_VERSION;
	data->max_pac_list_len = 10;

	if (config && config->phase1 &&
	    eap_fast_parse_phase1(data, config->phase1) < 0) {
		eap_fast_deinit(sm, data);
		return NULL;
	}

	if (eap_peer_select_phase2_methods(config, "auth=",
					   &data->phase2_types,
					   &data->num_phase2_types) < 0) {
		eap_fast_deinit(sm, data);
		return NULL;
	}

	data->phase2_type.vendor = EAP_VENDOR_IETF;
	data->phase2_type.method = EAP_TYPE_NONE;

	if (eap_peer_tls_ssl_init(sm, &data->ssl, config)) {
		wpa_printf(MSG_INFO, "EAP-FAST: Failed to initialize SSL.");
		eap_fast_deinit(sm, data);
		return NULL;
	}

	if (tls_connection_set_session_ticket_cb(sm->ssl_ctx, data->ssl.conn,
						 eap_fast_session_ticket_cb,
						 data) < 0) {
		wpa_printf(MSG_INFO, "EAP-FAST: Failed to set SessionTicket "
			   "callback");
		eap_fast_deinit(sm, data);
		return NULL;
	}

	/*
	 * The local RADIUS server in a Cisco AP does not seem to like empty
	 * fragments before data, so disable that workaround for CBC.
	 * TODO: consider making this configurable
	 */
	if (tls_connection_enable_workaround(sm->ssl_ctx, data->ssl.conn)) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Failed to enable TLS "
			   "workarounds");
	}

	if (data->use_pac_binary_format &&
	    eap_fast_load_pac_bin(sm, &data->pac, config->pac_file) < 0) {
		eap_fast_deinit(sm, data);
		return NULL;
	}

	if (!data->use_pac_binary_format &&
	    eap_fast_load_pac(sm, &data->pac, config->pac_file) < 0) {
		eap_fast_deinit(sm, data);
		return NULL;
	}
	eap_fast_pac_list_truncate(data->pac, data->max_pac_list_len);

	if (data->pac == NULL && !data->provisioning_allowed) {
		wpa_printf(MSG_INFO, "EAP-FAST: No PAC configured and "
			   "provisioning disabled");
		eap_fast_deinit(sm, data);
		return NULL;
	}

	return data;
}


static void eap_fast_deinit(struct eap_sm *sm, void *priv)
{
	struct eap_fast_data *data = priv;
	struct eap_fast_pac *pac, *prev;

	if (data == NULL)
		return;
	if (data->phase2_priv && data->phase2_method)
		data->phase2_method->deinit(sm, data->phase2_priv);
	os_free(data->phase2_types);
	os_free(data->key_block_p);
	eap_peer_tls_ssl_deinit(sm, &data->ssl);

	pac = data->pac;
	prev = NULL;
	while (pac) {
		prev = pac;
		pac = pac->next;
		eap_fast_free_pac(prev);
	}
	wpabuf_free(data->pending_phase2_req);
	os_free(data);
}


static int eap_fast_derive_msk(struct eap_fast_data *data)
{
	eap_fast_derive_eap_msk(data->simck, data->key_data);
	eap_fast_derive_eap_emsk(data->simck, data->emsk);
	data->success = 1;
	return 0;
}


static void eap_fast_derive_key_auth(struct eap_sm *sm,
				     struct eap_fast_data *data)
{
	u8 *sks;

	/* RFC 4851, Section 5.1:
	 * Extra key material after TLS key_block: session_key_seed[40]
	 */

	sks = eap_fast_derive_key(sm->ssl_ctx, data->ssl.conn, "key expansion",
				  EAP_FAST_SKS_LEN);
	if (sks == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Failed to derive "
			   "session_key_seed");
		return;
	}

	/*
	 * RFC 4851, Section 5.2:
	 * S-IMCK[0] = session_key_seed
	 */
	wpa_hexdump_key(MSG_DEBUG,
			"EAP-FAST: session_key_seed (SKS = S-IMCK[0])",
			sks, EAP_FAST_SKS_LEN);
	data->simck_idx = 0;
	os_memcpy(data->simck, sks, EAP_FAST_SIMCK_LEN);
	os_free(sks);
}


static void eap_fast_derive_key_provisioning(struct eap_sm *sm,
					     struct eap_fast_data *data)
{
	os_free(data->key_block_p);
	data->key_block_p = (struct eap_fast_key_block_provisioning *)
		eap_fast_derive_key(sm->ssl_ctx, data->ssl.conn,
				    "key expansion",
				    sizeof(*data->key_block_p));
	if (data->key_block_p == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Failed to derive key block");
		return;
	}
	/*
	 * RFC 4851, Section 5.2:
	 * S-IMCK[0] = session_key_seed
	 */
	wpa_hexdump_key(MSG_DEBUG,
			"EAP-FAST: session_key_seed (SKS = S-IMCK[0])",
			data->key_block_p->session_key_seed,
			sizeof(data->key_block_p->session_key_seed));
	data->simck_idx = 0;
	os_memcpy(data->simck, data->key_block_p->session_key_seed,
		  EAP_FAST_SIMCK_LEN);
	wpa_hexdump_key(MSG_DEBUG, "EAP-FAST: server_challenge",
			data->key_block_p->server_challenge,
			sizeof(data->key_block_p->server_challenge));
	wpa_hexdump_key(MSG_DEBUG, "EAP-FAST: client_challenge",
			data->key_block_p->client_challenge,
			sizeof(data->key_block_p->client_challenge));
}


static void eap_fast_derive_keys(struct eap_sm *sm, struct eap_fast_data *data)
{
	if (data->anon_provisioning)
		eap_fast_derive_key_provisioning(sm, data);
	else
		eap_fast_derive_key_auth(sm, data);
}


static int eap_fast_init_phase2_method(struct eap_sm *sm,
				       struct eap_fast_data *data)
{
	data->phase2_method =
		eap_peer_get_eap_method(data->phase2_type.vendor,
					data->phase2_type.method);
	if (data->phase2_method == NULL)
		return -1;

	if (data->key_block_p) {
		sm->auth_challenge = data->key_block_p->server_challenge;
		sm->peer_challenge = data->key_block_p->client_challenge;
	}
	sm->init_phase2 = 1;
	data->phase2_priv = data->phase2_method->init(sm);
	sm->init_phase2 = 0;
	sm->auth_challenge = NULL;
	sm->peer_challenge = NULL;

	return data->phase2_priv == NULL ? -1 : 0;
}


static int eap_fast_select_phase2_method(struct eap_fast_data *data, u8 type)
{
	size_t i;

	/* TODO: TNC with anonymous provisioning; need to require both
	 * completed MSCHAPv2 and TNC */

	if (data->anon_provisioning && type != EAP_TYPE_MSCHAPV2) {
		wpa_printf(MSG_INFO, "EAP-FAST: Only EAP-MSCHAPv2 is allowed "
			   "during unauthenticated provisioning; reject phase2"
			   " type %d", type);
		return -1;
	}

#ifdef EAP_TNC
	if (type == EAP_TYPE_TNC) {
		data->phase2_type.vendor = EAP_VENDOR_IETF;
		data->phase2_type.method = EAP_TYPE_TNC;
		wpa_printf(MSG_DEBUG, "EAP-FAST: Selected Phase 2 EAP "
			   "vendor %d method %d for TNC",
			   data->phase2_type.vendor,
			   data->phase2_type.method);
		return 0;
	}
#endif /* EAP_TNC */

	for (i = 0; i < data->num_phase2_types; i++) {
		if (data->phase2_types[i].vendor != EAP_VENDOR_IETF ||
		    data->phase2_types[i].method != type)
			continue;

		data->phase2_type.vendor = data->phase2_types[i].vendor;
		data->phase2_type.method = data->phase2_types[i].method;
		wpa_printf(MSG_DEBUG, "EAP-FAST: Selected Phase 2 EAP "
			   "vendor %d method %d",
			   data->phase2_type.vendor,
			   data->phase2_type.method);
		break;
	}

	if (type != data->phase2_type.method || type == EAP_TYPE_NONE)
		return -1;

	return 0;
}


static int eap_fast_phase2_request(struct eap_sm *sm,
				   struct eap_fast_data *data,
				   struct eap_method_ret *ret,
				   struct eap_hdr *hdr,
				   struct wpabuf **resp)
{
	size_t len = be_to_host16(hdr->length);
	u8 *pos;
	struct eap_method_ret iret;
	struct eap_peer_config *config = eap_get_config(sm);
	struct wpabuf msg;

	if (len <= sizeof(struct eap_hdr)) {
		wpa_printf(MSG_INFO, "EAP-FAST: too short "
			   "Phase 2 request (len=%lu)", (unsigned long) len);
		return -1;
	}
	pos = (u8 *) (hdr + 1);
	wpa_printf(MSG_DEBUG, "EAP-FAST: Phase 2 Request: type=%d", *pos);
	if (*pos == EAP_TYPE_IDENTITY) {
		*resp = eap_sm_buildIdentity(sm, hdr->identifier, 1);
		return 0;
	}

	if (data->phase2_priv && data->phase2_method &&
	    *pos != data->phase2_type.method) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Phase 2 EAP sequence - "
			   "deinitialize previous method");
		data->phase2_method->deinit(sm, data->phase2_priv);
		data->phase2_method = NULL;
		data->phase2_priv = NULL;
		data->phase2_type.vendor = EAP_VENDOR_IETF;
		data->phase2_type.method = EAP_TYPE_NONE;
	}

	if (data->phase2_type.vendor == EAP_VENDOR_IETF &&
	    data->phase2_type.method == EAP_TYPE_NONE &&
	    eap_fast_select_phase2_method(data, *pos) < 0) {
		if (eap_peer_tls_phase2_nak(data->phase2_types,
					    data->num_phase2_types,
					    hdr, resp))
			return -1;
		return 0;
	}

	if (data->phase2_priv == NULL &&
	    eap_fast_init_phase2_method(sm, data) < 0) {
		wpa_printf(MSG_INFO, "EAP-FAST: Failed to initialize "
			   "Phase 2 EAP method %d", *pos);
		ret->methodState = METHOD_DONE;
		ret->decision = DECISION_FAIL;
		return -1;
	}

	os_memset(&iret, 0, sizeof(iret));
	wpabuf_set(&msg, hdr, len);
	*resp = data->phase2_method->process(sm, data->phase2_priv, &iret,
					     &msg);
	if (*resp == NULL ||
	    (iret.methodState == METHOD_DONE &&
	     iret.decision == DECISION_FAIL)) {
		ret->methodState = METHOD_DONE;
		ret->decision = DECISION_FAIL;
	} else if ((iret.methodState == METHOD_DONE ||
		    iret.methodState == METHOD_MAY_CONT) &&
		   (iret.decision == DECISION_UNCOND_SUCC ||
		    iret.decision == DECISION_COND_SUCC)) {
		data->phase2_success = 1;
	}

	if (*resp == NULL && config &&
	    (config->pending_req_identity || config->pending_req_password ||
	     config->pending_req_otp || config->pending_req_new_password)) {
		wpabuf_free(data->pending_phase2_req);
		data->pending_phase2_req = wpabuf_alloc_copy(hdr, len);
	} else if (*resp == NULL)
		return -1;

	return 0;
}


static struct wpabuf * eap_fast_tlv_nak(int vendor_id, int tlv_type)
{
	struct wpabuf *buf;
	struct eap_tlv_nak_tlv *nak;
	buf = wpabuf_alloc(sizeof(*nak));
	if (buf == NULL)
		return NULL;
	nak = wpabuf_put(buf, sizeof(*nak));
	nak->tlv_type = host_to_be16(EAP_TLV_TYPE_MANDATORY | EAP_TLV_NAK_TLV);
	nak->length = host_to_be16(6);
	nak->vendor_id = host_to_be32(vendor_id);
	nak->nak_type = host_to_be16(tlv_type);
	return buf;
}


static struct wpabuf * eap_fast_tlv_result(int status, int intermediate)
{
	struct wpabuf *buf;
	struct eap_tlv_intermediate_result_tlv *result;
	buf = wpabuf_alloc(sizeof(*result));
	if (buf == NULL)
		return NULL;
	wpa_printf(MSG_DEBUG, "EAP-FAST: Add %sResult TLV(status=%d)",
		   intermediate ? "Intermediate " : "", status);
	result = wpabuf_put(buf, sizeof(*result));
	result->tlv_type = host_to_be16(EAP_TLV_TYPE_MANDATORY |
					(intermediate ?
					 EAP_TLV_INTERMEDIATE_RESULT_TLV :
					 EAP_TLV_RESULT_TLV));
	result->length = host_to_be16(2);
	result->status = host_to_be16(status);
	return buf;
}


static struct wpabuf * eap_fast_tlv_pac_ack(void)
{
	struct wpabuf *buf;
	struct eap_tlv_result_tlv *res;
	struct eap_tlv_pac_ack_tlv *ack;

	buf = wpabuf_alloc(sizeof(*res) + sizeof(*ack));
	if (buf == NULL)
		return NULL;

	wpa_printf(MSG_DEBUG, "EAP-FAST: Add PAC TLV (ack)");
	ack = wpabuf_put(buf, sizeof(*ack));
	ack->tlv_type = host_to_be16(EAP_TLV_PAC_TLV |
				     EAP_TLV_TYPE_MANDATORY);
	ack->length = host_to_be16(sizeof(*ack) - sizeof(struct eap_tlv_hdr));
	ack->pac_type = host_to_be16(PAC_TYPE_PAC_ACKNOWLEDGEMENT);
	ack->pac_len = host_to_be16(2);
	ack->result = host_to_be16(EAP_TLV_RESULT_SUCCESS);

	return buf;
}


static struct wpabuf * eap_fast_process_eap_payload_tlv(
	struct eap_sm *sm, struct eap_fast_data *data,
	struct eap_method_ret *ret, const struct eap_hdr *req,
	u8 *eap_payload_tlv, size_t eap_payload_tlv_len)
{
	struct eap_hdr *hdr;
	struct wpabuf *resp = NULL;

	if (eap_payload_tlv_len < sizeof(*hdr)) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: too short EAP "
			   "Payload TLV (len=%lu)",
			   (unsigned long) eap_payload_tlv_len);
		return NULL;
	}

	hdr = (struct eap_hdr *) eap_payload_tlv;
	if (be_to_host16(hdr->length) > eap_payload_tlv_len) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: EAP packet overflow in "
			   "EAP Payload TLV");
		return NULL;
	}

	if (hdr->code != EAP_CODE_REQUEST) {
		wpa_printf(MSG_INFO, "EAP-FAST: Unexpected code=%d in "
			   "Phase 2 EAP header", hdr->code);
		return NULL;
	}

	if (eap_fast_phase2_request(sm, data, ret, hdr, &resp)) {
		wpa_printf(MSG_INFO, "EAP-FAST: Phase2 Request processing "
			   "failed");
		return NULL;
	}

	return eap_fast_tlv_eap_payload(resp);
}


static int eap_fast_validate_crypto_binding(
	struct eap_tlv_crypto_binding_tlv *_bind)
{
	wpa_printf(MSG_DEBUG, "EAP-FAST: Crypto-Binding TLV: Version %d "
		   "Received Version %d SubType %d",
		   _bind->version, _bind->received_version, _bind->subtype);
	wpa_hexdump(MSG_MSGDUMP, "EAP-FAST: NONCE",
		    _bind->nonce, sizeof(_bind->nonce));
	wpa_hexdump(MSG_MSGDUMP, "EAP-FAST: Compound MAC",
		    _bind->compound_mac, sizeof(_bind->compound_mac));

	if (_bind->version != EAP_FAST_VERSION ||
	    _bind->received_version != EAP_FAST_VERSION ||
	    _bind->subtype != EAP_TLV_CRYPTO_BINDING_SUBTYPE_REQUEST) {
		wpa_printf(MSG_INFO, "EAP-FAST: Invalid version/subtype in "
			   "Crypto-Binding TLV: Version %d "
			   "Received Version %d SubType %d",
			   _bind->version, _bind->received_version,
			   _bind->subtype);
		return -1;
	}

	return 0;
}


static void eap_fast_write_crypto_binding(
	struct eap_tlv_crypto_binding_tlv *rbind,
	struct eap_tlv_crypto_binding_tlv *_bind, const u8 *cmk)
{
	rbind->tlv_type = host_to_be16(EAP_TLV_TYPE_MANDATORY |
				       EAP_TLV_CRYPTO_BINDING_TLV);
	rbind->length = host_to_be16(sizeof(*rbind) -
				     sizeof(struct eap_tlv_hdr));
	rbind->version = EAP_FAST_VERSION;
	rbind->received_version = _bind->version;
	rbind->subtype = EAP_TLV_CRYPTO_BINDING_SUBTYPE_RESPONSE;
	os_memcpy(rbind->nonce, _bind->nonce, sizeof(_bind->nonce));
	inc_byte_array(rbind->nonce, sizeof(rbind->nonce));
	hmac_sha1(cmk, EAP_FAST_CMK_LEN, (u8 *) rbind, sizeof(*rbind),
		  rbind->compound_mac);

	wpa_printf(MSG_DEBUG, "EAP-FAST: Reply Crypto-Binding TLV: Version %d "
		   "Received Version %d SubType %d",
		   rbind->version, rbind->received_version, rbind->subtype);
	wpa_hexdump(MSG_MSGDUMP, "EAP-FAST: NONCE",
		    rbind->nonce, sizeof(rbind->nonce));
	wpa_hexdump(MSG_MSGDUMP, "EAP-FAST: Compound MAC",
		    rbind->compound_mac, sizeof(rbind->compound_mac));
}


static int eap_fast_get_phase2_key(struct eap_sm *sm,
				   struct eap_fast_data *data,
				   u8 *isk, size_t isk_len)
{
	u8 *key;
	size_t key_len;

	os_memset(isk, 0, isk_len);

	if (data->phase2_method == NULL || data->phase2_priv == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Phase 2 method not "
			   "available");
		return -1;
	}

	if (data->phase2_method->isKeyAvailable == NULL ||
	    data->phase2_method->getKey == NULL)
		return 0;

	if (!data->phase2_method->isKeyAvailable(sm, data->phase2_priv) ||
	    (key = data->phase2_method->getKey(sm, data->phase2_priv,
					       &key_len)) == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Could not get key material "
			   "from Phase 2");
		return -1;
	}

	if (key_len > isk_len)
		key_len = isk_len;
	if (key_len == 32 &&
	    data->phase2_method->vendor == EAP_VENDOR_IETF &&
	    data->phase2_method->method == EAP_TYPE_MSCHAPV2) {
		/*
		 * EAP-FAST uses reverse order for MS-MPPE keys when deriving
		 * MSK from EAP-MSCHAPv2. Swap the keys here to get the correct
		 * ISK for EAP-FAST cryptobinding.
		 */
		os_memcpy(isk, key + 16, 16);
		os_memcpy(isk + 16, key, 16);
	} else
		os_memcpy(isk, key, key_len);
	os_free(key);

	return 0;
}


static int eap_fast_get_cmk(struct eap_sm *sm, struct eap_fast_data *data,
			    u8 *cmk)
{
	u8 isk[32], imck[60];

	wpa_printf(MSG_DEBUG, "EAP-FAST: Determining CMK[%d] for Compound MIC "
		   "calculation", data->simck_idx + 1);

	/*
	 * RFC 4851, Section 5.2:
	 * IMCK[j] = T-PRF(S-IMCK[j-1], "Inner Methods Compound Keys",
	 *                 MSK[j], 60)
	 * S-IMCK[j] = first 40 octets of IMCK[j]
	 * CMK[j] = last 20 octets of IMCK[j]
	 */

	if (eap_fast_get_phase2_key(sm, data, isk, sizeof(isk)) < 0)
		return -1;
	wpa_hexdump_key(MSG_MSGDUMP, "EAP-FAST: ISK[j]", isk, sizeof(isk));
	sha1_t_prf(data->simck, EAP_FAST_SIMCK_LEN,
		   "Inner Methods Compound Keys",
		   isk, sizeof(isk), imck, sizeof(imck));
	data->simck_idx++;
	os_memcpy(data->simck, imck, EAP_FAST_SIMCK_LEN);
	wpa_hexdump_key(MSG_MSGDUMP, "EAP-FAST: S-IMCK[j]",
			data->simck, EAP_FAST_SIMCK_LEN);
	os_memcpy(cmk, imck + EAP_FAST_SIMCK_LEN, EAP_FAST_CMK_LEN);
	wpa_hexdump_key(MSG_MSGDUMP, "EAP-FAST: CMK[j]",
			cmk, EAP_FAST_CMK_LEN);

	return 0;
}


static u8 * eap_fast_write_pac_request(u8 *pos, u16 pac_type)
{
	struct eap_tlv_hdr *pac;
	struct eap_tlv_request_action_tlv *act;
	struct eap_tlv_pac_type_tlv *type;

	act = (struct eap_tlv_request_action_tlv *) pos;
	act->tlv_type = host_to_be16(EAP_TLV_REQUEST_ACTION_TLV);
	act->length = host_to_be16(2);
	act->action = host_to_be16(EAP_TLV_ACTION_PROCESS_TLV);

	pac = (struct eap_tlv_hdr *) (act + 1);
	pac->tlv_type = host_to_be16(EAP_TLV_PAC_TLV);
	pac->length = host_to_be16(sizeof(*type));

	type = (struct eap_tlv_pac_type_tlv *) (pac + 1);
	type->tlv_type = host_to_be16(PAC_TYPE_PAC_TYPE);
	type->length = host_to_be16(2);
	type->pac_type = host_to_be16(pac_type);

	return (u8 *) (type + 1);
}


static struct wpabuf * eap_fast_process_crypto_binding(
	struct eap_sm *sm, struct eap_fast_data *data,
	struct eap_method_ret *ret,
	struct eap_tlv_crypto_binding_tlv *_bind, size_t bind_len)
{
	struct wpabuf *resp;
	u8 *pos;
	u8 cmk[EAP_FAST_CMK_LEN], cmac[SHA1_MAC_LEN];
	int res;
	size_t len;

	if (eap_fast_validate_crypto_binding(_bind) < 0)
		return NULL;

	if (eap_fast_get_cmk(sm, data, cmk) < 0)
		return NULL;

	/* Validate received Compound MAC */
	os_memcpy(cmac, _bind->compound_mac, sizeof(cmac));
	os_memset(_bind->compound_mac, 0, sizeof(cmac));
	wpa_hexdump(MSG_MSGDUMP, "EAP-FAST: Crypto-Binding TLV for Compound "
		    "MAC calculation", (u8 *) _bind, bind_len);
	hmac_sha1(cmk, EAP_FAST_CMK_LEN, (u8 *) _bind, bind_len,
		  _bind->compound_mac);
	res = os_memcmp(cmac, _bind->compound_mac, sizeof(cmac));
	wpa_hexdump(MSG_MSGDUMP, "EAP-FAST: Received Compound MAC",
		    cmac, sizeof(cmac));
	wpa_hexdump(MSG_MSGDUMP, "EAP-FAST: Calculated Compound MAC",
		    _bind->compound_mac, sizeof(cmac));
	if (res != 0) {
		wpa_printf(MSG_INFO, "EAP-FAST: Compound MAC did not match");
		os_memcpy(_bind->compound_mac, cmac, sizeof(cmac));
		return NULL;
	}

	/*
	 * Compound MAC was valid, so authentication succeeded. Reply with
	 * crypto binding to allow server to complete authentication.
	 */

	len = sizeof(struct eap_tlv_crypto_binding_tlv);
	resp = wpabuf_alloc(len);
	if (resp == NULL)
		return NULL;

	if (!data->anon_provisioning && data->phase2_success &&
	    eap_fast_derive_msk(data) < 0) {
		wpa_printf(MSG_INFO, "EAP-FAST: Failed to generate MSK");
		ret->methodState = METHOD_DONE;
		ret->decision = DECISION_FAIL;
		data->phase2_success = 0;
		wpabuf_free(resp);
		return NULL;
	}

	pos = wpabuf_put(resp, sizeof(struct eap_tlv_crypto_binding_tlv));
	eap_fast_write_crypto_binding((struct eap_tlv_crypto_binding_tlv *)
				      pos, _bind, cmk);

	return resp;
}


static void eap_fast_parse_pac_tlv(struct eap_fast_pac *entry, int type,
				   u8 *pos, size_t len, int *pac_key_found)
{
	switch (type & 0x7fff) {
	case PAC_TYPE_PAC_KEY:
		wpa_hexdump_key(MSG_DEBUG, "EAP-FAST: PAC-Key", pos, len);
		if (len != EAP_FAST_PAC_KEY_LEN) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: Invalid PAC-Key "
				   "length %lu", (unsigned long) len);
			break;
		}
		*pac_key_found = 1;
		os_memcpy(entry->pac_key, pos, len);
		break;
	case PAC_TYPE_PAC_OPAQUE:
		wpa_hexdump(MSG_DEBUG, "EAP-FAST: PAC-Opaque", pos, len);
		entry->pac_opaque = pos;
		entry->pac_opaque_len = len;
		break;
	case PAC_TYPE_PAC_INFO:
		wpa_hexdump(MSG_DEBUG, "EAP-FAST: PAC-Info", pos, len);
		entry->pac_info = pos;
		entry->pac_info_len = len;
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-FAST: Ignored unknown PAC type %d",
			   type);
		break;
	}
}


static int eap_fast_process_pac_tlv(struct eap_fast_pac *entry,
				    u8 *pac, size_t pac_len)
{
	struct pac_tlv_hdr *hdr;
	u8 *pos;
	size_t left, len;
	int type, pac_key_found = 0;

	pos = pac;
	left = pac_len;

	while (left > sizeof(*hdr)) {
		hdr = (struct pac_tlv_hdr *) pos;
		type = be_to_host16(hdr->type);
		len = be_to_host16(hdr->len);
		pos += sizeof(*hdr);
		left -= sizeof(*hdr);
		if (len > left) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: PAC TLV overrun "
				   "(type=%d len=%lu left=%lu)",
				   type, (unsigned long) len,
				   (unsigned long) left);
			return -1;
		}

		eap_fast_parse_pac_tlv(entry, type, pos, len, &pac_key_found);

		pos += len;
		left -= len;
	}

	if (!pac_key_found || !entry->pac_opaque || !entry->pac_info) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: PAC TLV does not include "
			   "all the required fields");
		return -1;
	}

	return 0;
}


static int eap_fast_parse_pac_info(struct eap_fast_pac *entry, int type,
				   u8 *pos, size_t len)
{
	u16 pac_type;
	u32 lifetime;
	struct os_time now;

	switch (type & 0x7fff) {
	case PAC_TYPE_CRED_LIFETIME:
		if (len != 4) {
			wpa_hexdump(MSG_DEBUG, "EAP-FAST: PAC-Info - "
				    "Invalid CRED_LIFETIME length - ignored",
				    pos, len);
			return 0;
		}

		/*
		 * This is not currently saved separately in PAC files since
		 * the server can automatically initiate PAC update when
		 * needed. Anyway, the information is available from PAC-Info
		 * dump if it is needed for something in the future.
		 */
		lifetime = WPA_GET_BE32(pos);
		os_get_time(&now);
		wpa_printf(MSG_DEBUG, "EAP-FAST: PAC-Info - CRED_LIFETIME %d "
			   "(%d days)",
			   lifetime, (lifetime - (u32) now.sec) / 86400);
		break;
	case PAC_TYPE_A_ID:
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-FAST: PAC-Info - A-ID",
				  pos, len);
		entry->a_id = pos;
		entry->a_id_len = len;
		break;
	case PAC_TYPE_I_ID:
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-FAST: PAC-Info - I-ID",
				  pos, len);
		entry->i_id = pos;
		entry->i_id_len = len;
		break;
	case PAC_TYPE_A_ID_INFO:
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-FAST: PAC-Info - A-ID-Info",
				  pos, len);
		entry->a_id_info = pos;
		entry->a_id_info_len = len;
		break;
	case PAC_TYPE_PAC_TYPE:
		/* RFC 5422, Section 4.2.6 - PAC-Type TLV */
		if (len != 2) {
			wpa_printf(MSG_INFO, "EAP-FAST: Invalid PAC-Type "
				   "length %lu (expected 2)",
				   (unsigned long) len);
			wpa_hexdump_ascii(MSG_DEBUG,
					  "EAP-FAST: PAC-Info - PAC-Type",
					  pos, len);
			return -1;
		}
		pac_type = WPA_GET_BE16(pos);
		if (pac_type != PAC_TYPE_TUNNEL_PAC &&
		    pac_type != PAC_TYPE_USER_AUTHORIZATION &&
		    pac_type != PAC_TYPE_MACHINE_AUTHENTICATION) {
			wpa_printf(MSG_INFO, "EAP-FAST: Unsupported PAC Type "
				   "%d", pac_type);
			return -1;
		}

		wpa_printf(MSG_DEBUG, "EAP-FAST: PAC-Info - PAC-Type %d",
			   pac_type);
		entry->pac_type = pac_type;
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-FAST: Ignored unknown PAC-Info "
			   "type %d", type);
		break;
	}

	return 0;
}


static int eap_fast_process_pac_info(struct eap_fast_pac *entry)
{
	struct pac_tlv_hdr *hdr;
	u8 *pos;
	size_t left, len;
	int type;

	/* RFC 5422, Section 4.2.4 */

	/* PAC-Type defaults to Tunnel PAC (Type 1) */
	entry->pac_type = PAC_TYPE_TUNNEL_PAC;

	pos = entry->pac_info;
	left = entry->pac_info_len;
	while (left > sizeof(*hdr)) {
		hdr = (struct pac_tlv_hdr *) pos;
		type = be_to_host16(hdr->type);
		len = be_to_host16(hdr->len);
		pos += sizeof(*hdr);
		left -= sizeof(*hdr);
		if (len > left) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: PAC-Info overrun "
				   "(type=%d len=%lu left=%lu)",
				   type, (unsigned long) len,
				   (unsigned long) left);
			return -1;
		}

		if (eap_fast_parse_pac_info(entry, type, pos, len) < 0)
			return -1;

		pos += len;
		left -= len;
	}

	if (entry->a_id == NULL || entry->a_id_info == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: PAC-Info does not include "
			   "all the required fields");
		return -1;
	}

	return 0;
}


static struct wpabuf * eap_fast_process_pac(struct eap_sm *sm,
					    struct eap_fast_data *data,
					    struct eap_method_ret *ret,
					    u8 *pac, size_t pac_len)
{
	struct eap_peer_config *config = eap_get_config(sm);
	struct eap_fast_pac entry;

	os_memset(&entry, 0, sizeof(entry));
	if (eap_fast_process_pac_tlv(&entry, pac, pac_len) ||
	    eap_fast_process_pac_info(&entry))
		return NULL;

	eap_fast_add_pac(&data->pac, &data->current_pac, &entry);
	eap_fast_pac_list_truncate(data->pac, data->max_pac_list_len);
	if (data->use_pac_binary_format)
		eap_fast_save_pac_bin(sm, data->pac, config->pac_file);
	else
		eap_fast_save_pac(sm, data->pac, config->pac_file);

	if (data->provisioning) {
		if (data->anon_provisioning) {
			/*
			 * Unauthenticated provisioning does not provide keying
			 * material and must end with an EAP-Failure.
			 * Authentication will be done separately after this.
			 */
			data->success = 0;
			ret->decision = DECISION_FAIL;
		} else {
			/*
			 * Server may or may not allow authenticated
			 * provisioning also for key generation.
			 */
			ret->decision = DECISION_COND_SUCC;
		}
		wpa_printf(MSG_DEBUG, "EAP-FAST: Send PAC-Acknowledgement TLV "
			   "- Provisioning completed successfully");
	} else {
		/*
		 * This is PAC refreshing, i.e., normal authentication that is
		 * expected to be completed with an EAP-Success.
		 */
		wpa_printf(MSG_DEBUG, "EAP-FAST: Send PAC-Acknowledgement TLV "
			   "- PAC refreshing completed successfully");
		ret->decision = DECISION_UNCOND_SUCC;
	}
	ret->methodState = METHOD_DONE;
	return eap_fast_tlv_pac_ack();
}


static int eap_fast_parse_decrypted(struct wpabuf *decrypted,
				    struct eap_fast_tlv_parse *tlv,
				    struct wpabuf **resp)
{
	int mandatory, tlv_type, len, res;
	u8 *pos, *end;

	os_memset(tlv, 0, sizeof(*tlv));

	/* Parse TLVs from the decrypted Phase 2 data */
	pos = wpabuf_mhead(decrypted);
	end = pos + wpabuf_len(decrypted);
	while (pos + 4 < end) {
		mandatory = pos[0] & 0x80;
		tlv_type = WPA_GET_BE16(pos) & 0x3fff;
		pos += 2;
		len = WPA_GET_BE16(pos);
		pos += 2;
		if (pos + len > end) {
			wpa_printf(MSG_INFO, "EAP-FAST: TLV overflow");
			return -1;
		}
		wpa_printf(MSG_DEBUG, "EAP-FAST: Received Phase 2: "
			   "TLV type %d length %d%s",
			   tlv_type, len, mandatory ? " (mandatory)" : "");

		res = eap_fast_parse_tlv(tlv, tlv_type, pos, len);
		if (res == -2)
			break;
		if (res < 0) {
			if (mandatory) {
				wpa_printf(MSG_DEBUG, "EAP-FAST: Nak unknown "
					   "mandatory TLV type %d", tlv_type);
				*resp = eap_fast_tlv_nak(0, tlv_type);
				break;
			} else {
				wpa_printf(MSG_DEBUG, "EAP-FAST: ignored "
					   "unknown optional TLV type %d",
					   tlv_type);
			}
		}

		pos += len;
	}

	return 0;
}


static int eap_fast_encrypt_response(struct eap_sm *sm,
				     struct eap_fast_data *data,
				     struct wpabuf *resp,
				     u8 identifier, struct wpabuf **out_data)
{
	if (resp == NULL)
		return 0;

	wpa_hexdump_buf(MSG_DEBUG, "EAP-FAST: Encrypting Phase 2 data",
			resp);
	if (eap_peer_tls_encrypt(sm, &data->ssl, EAP_TYPE_FAST,
				 data->fast_version, identifier,
				 resp, out_data)) {
		wpa_printf(MSG_INFO, "EAP-FAST: Failed to encrypt a Phase 2 "
			   "frame");
	}
	wpabuf_free(resp);

	return 0;
}


static struct wpabuf * eap_fast_pac_request(void)
{
	struct wpabuf *tmp;
	u8 *pos, *pos2;

	tmp = wpabuf_alloc(sizeof(struct eap_tlv_hdr) +
			   sizeof(struct eap_tlv_request_action_tlv) +
			   sizeof(struct eap_tlv_pac_type_tlv));
	if (tmp == NULL)
		return NULL;

	pos = wpabuf_put(tmp, 0);
	pos2 = eap_fast_write_pac_request(pos, PAC_TYPE_TUNNEL_PAC);
	wpabuf_put(tmp, pos2 - pos);
	return tmp;
}


static int eap_fast_process_decrypted(struct eap_sm *sm,
				      struct eap_fast_data *data,
				      struct eap_method_ret *ret,
				      const struct eap_hdr *req,
				      struct wpabuf *decrypted,
				      struct wpabuf **out_data)
{
	struct wpabuf *resp = NULL, *tmp;
	struct eap_fast_tlv_parse tlv;
	int failed = 0;

	if (eap_fast_parse_decrypted(decrypted, &tlv, &resp) < 0)
		return 0;
	if (resp)
		return eap_fast_encrypt_response(sm, data, resp,
						 req->identifier, out_data);

	if (tlv.result == EAP_TLV_RESULT_FAILURE) {
		resp = eap_fast_tlv_result(EAP_TLV_RESULT_FAILURE, 0);
		return eap_fast_encrypt_response(sm, data, resp,
						 req->identifier, out_data);
	}

	if (tlv.iresult == EAP_TLV_RESULT_FAILURE) {
		resp = eap_fast_tlv_result(EAP_TLV_RESULT_FAILURE, 1);
		return eap_fast_encrypt_response(sm, data, resp,
						 req->identifier, out_data);
	}

	if (tlv.crypto_binding) {
		tmp = eap_fast_process_crypto_binding(sm, data, ret,
						      tlv.crypto_binding,
						      tlv.crypto_binding_len);
		if (tmp == NULL)
			failed = 1;
		else
			resp = wpabuf_concat(resp, tmp);
	}

	if (tlv.iresult == EAP_TLV_RESULT_SUCCESS) {
		tmp = eap_fast_tlv_result(failed ? EAP_TLV_RESULT_FAILURE :
					  EAP_TLV_RESULT_SUCCESS, 1);
		resp = wpabuf_concat(resp, tmp);
	}

	if (tlv.eap_payload_tlv) {
		tmp = eap_fast_process_eap_payload_tlv(
			sm, data, ret, req, tlv.eap_payload_tlv,
			tlv.eap_payload_tlv_len);
		resp = wpabuf_concat(resp, tmp);
	}

	if (tlv.pac && tlv.result != EAP_TLV_RESULT_SUCCESS) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: PAC TLV without Result TLV "
			   "acknowledging success");
		failed = 1;
	} else if (tlv.pac && tlv.result == EAP_TLV_RESULT_SUCCESS) {
		tmp = eap_fast_process_pac(sm, data, ret, tlv.pac,
					   tlv.pac_len);
		resp = wpabuf_concat(resp, tmp);
	}

	if (data->current_pac == NULL && data->provisioning &&
	    !data->anon_provisioning && !tlv.pac &&
	    (tlv.iresult == EAP_TLV_RESULT_SUCCESS ||
	     tlv.result == EAP_TLV_RESULT_SUCCESS)) {
		/*
		 * Need to request Tunnel PAC when using authenticated
		 * provisioning.
		 */
		wpa_printf(MSG_DEBUG, "EAP-FAST: Request Tunnel PAC");
		tmp = eap_fast_pac_request();
		resp = wpabuf_concat(resp, tmp);
	}

	if (tlv.result == EAP_TLV_RESULT_SUCCESS && !failed) {
		tmp = eap_fast_tlv_result(EAP_TLV_RESULT_SUCCESS, 0);
		resp = wpabuf_concat(tmp, resp);
	} else if (failed) {
		tmp = eap_fast_tlv_result(EAP_TLV_RESULT_FAILURE, 0);
		resp = wpabuf_concat(tmp, resp);
	}

	if (resp && tlv.result == EAP_TLV_RESULT_SUCCESS && !failed &&
	    tlv.crypto_binding && data->phase2_success) {
		if (data->anon_provisioning) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: Unauthenticated "
				   "provisioning completed successfully.");
			ret->methodState = METHOD_DONE;
			ret->decision = DECISION_FAIL;
		} else {
			wpa_printf(MSG_DEBUG, "EAP-FAST: Authentication "
				   "completed successfully.");
			if (data->provisioning)
				ret->methodState = METHOD_MAY_CONT;
			else
				ret->methodState = METHOD_DONE;
			ret->decision = DECISION_UNCOND_SUCC;
		}
	}

	if (resp == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: No recognized TLVs - send "
			   "empty response packet");
		resp = wpabuf_alloc(1);
	}

	return eap_fast_encrypt_response(sm, data, resp, req->identifier,
					 out_data);
}


static int eap_fast_decrypt(struct eap_sm *sm, struct eap_fast_data *data,
			    struct eap_method_ret *ret,
			    const struct eap_hdr *req,
			    const struct wpabuf *in_data,
			    struct wpabuf **out_data)
{
	struct wpabuf *in_decrypted;
	int res;

	wpa_printf(MSG_DEBUG, "EAP-FAST: Received %lu bytes encrypted data for"
		   " Phase 2", (unsigned long) wpabuf_len(in_data));

	if (data->pending_phase2_req) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Pending Phase 2 request - "
			   "skip decryption and use old data");
		/* Clear TLS reassembly state. */
		eap_peer_tls_reset_input(&data->ssl);

		in_decrypted = data->pending_phase2_req;
		data->pending_phase2_req = NULL;
		goto continue_req;
	}

	if (wpabuf_len(in_data) == 0) {
		/* Received TLS ACK - requesting more fragments */
		return eap_peer_tls_encrypt(sm, &data->ssl, EAP_TYPE_FAST,
					    data->fast_version,
					    req->identifier, NULL, out_data);
	}

	res = eap_peer_tls_decrypt(sm, &data->ssl, in_data, &in_decrypted);
	if (res)
		return res;

continue_req:
	wpa_hexdump_buf(MSG_MSGDUMP, "EAP-FAST: Decrypted Phase 2 TLV(s)",
			in_decrypted);

	if (wpabuf_len(in_decrypted) < 4) {
		wpa_printf(MSG_INFO, "EAP-FAST: Too short Phase 2 "
			   "TLV frame (len=%lu)",
			   (unsigned long) wpabuf_len(in_decrypted));
		wpabuf_free(in_decrypted);
		return -1;
	}

	res = eap_fast_process_decrypted(sm, data, ret, req,
					 in_decrypted, out_data);

	wpabuf_free(in_decrypted);

	return res;
}


static const u8 * eap_fast_get_a_id(const u8 *buf, size_t len, size_t *id_len)
{
	const u8 *a_id;
	struct pac_tlv_hdr *hdr;

	/*
	 * Parse authority identity (A-ID) from the EAP-FAST/Start. This
	 * supports both raw A-ID and one inside an A-ID TLV.
	 */
	a_id = buf;
	*id_len = len;
	if (len > sizeof(*hdr)) {
		int tlen;
		hdr = (struct pac_tlv_hdr *) buf;
		tlen = be_to_host16(hdr->len);
		if (be_to_host16(hdr->type) == PAC_TYPE_A_ID &&
		    sizeof(*hdr) + tlen <= len) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: A-ID was in TLV "
				   "(Start)");
			a_id = (u8 *) (hdr + 1);
			*id_len = tlen;
		}
	}
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-FAST: A-ID", a_id, *id_len);

	return a_id;
}


static void eap_fast_select_pac(struct eap_fast_data *data,
				const u8 *a_id, size_t a_id_len)
{
	data->current_pac = eap_fast_get_pac(data->pac, a_id, a_id_len,
					     PAC_TYPE_TUNNEL_PAC);
	if (data->current_pac == NULL) {
		/*
		 * Tunnel PAC was not available for this A-ID. Try to use
		 * Machine Authentication PAC, if one is available.
		 */
		data->current_pac = eap_fast_get_pac(
			data->pac, a_id, a_id_len,
			PAC_TYPE_MACHINE_AUTHENTICATION);
	}

	if (data->current_pac) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: PAC found for this A-ID "
			   "(PAC-Type %d)", data->current_pac->pac_type);
		wpa_hexdump_ascii(MSG_MSGDUMP, "EAP-FAST: A-ID-Info",
				  data->current_pac->a_id_info,
				  data->current_pac->a_id_info_len);
	}
}


static int eap_fast_use_pac_opaque(struct eap_sm *sm,
				   struct eap_fast_data *data,
				   struct eap_fast_pac *pac)
{
	u8 *tlv;
	size_t tlv_len, olen;
	struct eap_tlv_hdr *ehdr;

	olen = pac->pac_opaque_len;
	tlv_len = sizeof(*ehdr) + olen;
	tlv = os_malloc(tlv_len);
	if (tlv) {
		ehdr = (struct eap_tlv_hdr *) tlv;
		ehdr->tlv_type = host_to_be16(PAC_TYPE_PAC_OPAQUE);
		ehdr->length = host_to_be16(olen);
		os_memcpy(ehdr + 1, pac->pac_opaque, olen);
	}
	if (tlv == NULL ||
	    tls_connection_client_hello_ext(sm->ssl_ctx, data->ssl.conn,
					    TLS_EXT_PAC_OPAQUE,
					    tlv, tlv_len) < 0) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Failed to add PAC-Opaque TLS "
			   "extension");
		os_free(tlv);
		return -1;
	}
	os_free(tlv);

	return 0;
}


static int eap_fast_clear_pac_opaque_ext(struct eap_sm *sm,
					 struct eap_fast_data *data)
{
	if (tls_connection_client_hello_ext(sm->ssl_ctx, data->ssl.conn,
					    TLS_EXT_PAC_OPAQUE, NULL, 0) < 0) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Failed to remove PAC-Opaque "
			   "TLS extension");
		return -1;
	}
	return 0;
}


static int eap_fast_set_provisioning_ciphers(struct eap_sm *sm,
					     struct eap_fast_data *data)
{
	u8 ciphers[5];
	int count = 0;

	if (data->provisioning_allowed & EAP_FAST_PROV_UNAUTH) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Enabling unauthenticated "
			   "provisioning TLS cipher suites");
		ciphers[count++] = TLS_CIPHER_ANON_DH_AES128_SHA;
	}

	if (data->provisioning_allowed & EAP_FAST_PROV_AUTH) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Enabling authenticated "
			   "provisioning TLS cipher suites");
		ciphers[count++] = TLS_CIPHER_RSA_DHE_AES128_SHA;
		ciphers[count++] = TLS_CIPHER_AES128_SHA;
		ciphers[count++] = TLS_CIPHER_RC4_SHA;
	}

	ciphers[count++] = TLS_CIPHER_NONE;

	if (tls_connection_set_cipher_list(sm->ssl_ctx, data->ssl.conn,
					   ciphers)) {
		wpa_printf(MSG_INFO, "EAP-FAST: Could not configure TLS "
			   "cipher suites for provisioning");
		return -1;
	}

	return 0;
}


static int eap_fast_process_start(struct eap_sm *sm,
				  struct eap_fast_data *data, u8 flags,
				  const u8 *pos, size_t left)
{
	const u8 *a_id;
	size_t a_id_len;

	/* EAP-FAST Version negotiation (section 3.1) */
	wpa_printf(MSG_DEBUG, "EAP-FAST: Start (server ver=%d, own ver=%d)",
		   flags & EAP_TLS_VERSION_MASK, data->fast_version);
	if ((flags & EAP_TLS_VERSION_MASK) < data->fast_version)
		data->fast_version = flags & EAP_TLS_VERSION_MASK;
	wpa_printf(MSG_DEBUG, "EAP-FAST: Using FAST version %d",
		   data->fast_version);

	a_id = eap_fast_get_a_id(pos, left, &a_id_len);
	eap_fast_select_pac(data, a_id, a_id_len);

	if (data->resuming && data->current_pac) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Trying to resume session - "
			   "do not add PAC-Opaque to TLS ClientHello");
		if (eap_fast_clear_pac_opaque_ext(sm, data) < 0)
			return -1;
	} else if (data->current_pac) {
		/*
		 * PAC found for the A-ID and we are not resuming an old
		 * session, so add PAC-Opaque extension to ClientHello.
		 */
		if (eap_fast_use_pac_opaque(sm, data, data->current_pac) < 0)
			return -1;
	} else {
		/* No PAC found, so we must provision one. */
		if (!data->provisioning_allowed) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: No PAC found and "
				   "provisioning disabled");
			return -1;
		}
		wpa_printf(MSG_DEBUG, "EAP-FAST: No PAC found - "
			   "starting provisioning");
		if (eap_fast_set_provisioning_ciphers(sm, data) < 0 ||
		    eap_fast_clear_pac_opaque_ext(sm, data) < 0)
			return -1;
		data->provisioning = 1;
	}

	return 0;
}


static struct wpabuf * eap_fast_process(struct eap_sm *sm, void *priv,
					struct eap_method_ret *ret,
					const struct wpabuf *reqData)
{
	const struct eap_hdr *req;
	size_t left;
	int res;
	u8 flags, id;
	struct wpabuf *resp;
	const u8 *pos;
	struct eap_fast_data *data = priv;

	pos = eap_peer_tls_process_init(sm, &data->ssl, EAP_TYPE_FAST, ret,
					reqData, &left, &flags);
	if (pos == NULL)
		return NULL;

	req = wpabuf_head(reqData);
	id = req->identifier;

	if (flags & EAP_TLS_FLAGS_START) {
		if (eap_fast_process_start(sm, data, flags, pos, left) < 0)
			return NULL;

		left = 0; /* A-ID is not used in further packet processing */
	}

	resp = NULL;
	if (tls_connection_established(sm->ssl_ctx, data->ssl.conn) &&
	    !data->resuming) {
		/* Process tunneled (encrypted) phase 2 data. */
		struct wpabuf msg;
		wpabuf_set(&msg, pos, left);
		res = eap_fast_decrypt(sm, data, ret, req, &msg, &resp);
		if (res < 0) {
			ret->methodState = METHOD_DONE;
			ret->decision = DECISION_FAIL;
			/*
			 * Ack possible Alert that may have caused failure in
			 * decryption.
			 */
			res = 1;
		}
	} else {
		/* Continue processing TLS handshake (phase 1). */
		res = eap_peer_tls_process_helper(sm, &data->ssl,
						  EAP_TYPE_FAST,
						  data->fast_version, id, pos,
						  left, &resp);

		if (tls_connection_established(sm->ssl_ctx, data->ssl.conn)) {
			char cipher[80];
			wpa_printf(MSG_DEBUG,
				   "EAP-FAST: TLS done, proceed to Phase 2");
			if (data->provisioning &&
			    (!(data->provisioning_allowed &
			       EAP_FAST_PROV_AUTH) ||
			     tls_get_cipher(sm->ssl_ctx, data->ssl.conn,
					    cipher, sizeof(cipher)) < 0 ||
			     os_strstr(cipher, "ADH-") ||
			     os_strstr(cipher, "anon"))) {
				wpa_printf(MSG_DEBUG, "EAP-FAST: Using "
					   "anonymous (unauthenticated) "
					   "provisioning");
				data->anon_provisioning = 1;
			} else
				data->anon_provisioning = 0;
			data->resuming = 0;
			eap_fast_derive_keys(sm, data);
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
			res = eap_fast_decrypt(sm, data, ret, req, &msg,
					       &resp);
		}
	}

	if (res == 1) {
		wpabuf_free(resp);
		return eap_peer_tls_build_ack(id, EAP_TYPE_FAST,
					      data->fast_version);
	}

	return resp;
}


#if 0 /* FIX */
static Boolean eap_fast_has_reauth_data(struct eap_sm *sm, void *priv)
{
	struct eap_fast_data *data = priv;
	return tls_connection_established(sm->ssl_ctx, data->ssl.conn);
}


static void eap_fast_deinit_for_reauth(struct eap_sm *sm, void *priv)
{
	struct eap_fast_data *data = priv;
	os_free(data->key_block_p);
	data->key_block_p = NULL;
	wpabuf_free(data->pending_phase2_req);
	data->pending_phase2_req = NULL;
}


static void * eap_fast_init_for_reauth(struct eap_sm *sm, void *priv)
{
	struct eap_fast_data *data = priv;
	if (eap_peer_tls_reauth_init(sm, &data->ssl)) {
		os_free(data);
		return NULL;
	}
	if (data->phase2_priv && data->phase2_method &&
	    data->phase2_method->init_for_reauth)
		data->phase2_method->init_for_reauth(sm, data->phase2_priv);
	data->phase2_success = 0;
	data->resuming = 1;
	data->provisioning = 0;
	data->anon_provisioning = 0;
	data->simck_idx = 0;
	return priv;
}
#endif


static int eap_fast_get_status(struct eap_sm *sm, void *priv, char *buf,
			       size_t buflen, int verbose)
{
	struct eap_fast_data *data = priv;
	int len, ret;

	len = eap_peer_tls_status(sm, &data->ssl, buf, buflen, verbose);
	if (data->phase2_method) {
		ret = os_snprintf(buf + len, buflen - len,
				  "EAP-FAST Phase2 method=%s\n",
				  data->phase2_method->name);
		if (ret < 0 || (size_t) ret >= buflen - len)
			return len;
		len += ret;
	}
	return len;
}


static Boolean eap_fast_isKeyAvailable(struct eap_sm *sm, void *priv)
{
	struct eap_fast_data *data = priv;
	return data->success;
}


static u8 * eap_fast_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_fast_data *data = priv;
	u8 *key;

	if (!data->success)
		return NULL;

	key = os_malloc(EAP_FAST_KEY_LEN);
	if (key == NULL)
		return NULL;

	*len = EAP_FAST_KEY_LEN;
	os_memcpy(key, data->key_data, EAP_FAST_KEY_LEN);

	return key;
}


static u8 * eap_fast_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_fast_data *data = priv;
	u8 *key;

	if (!data->success)
		return NULL;

	key = os_malloc(EAP_EMSK_LEN);
	if (key == NULL)
		return NULL;

	*len = EAP_EMSK_LEN;
	os_memcpy(key, data->emsk, EAP_EMSK_LEN);

	return key;
}


int eap_peer_fast_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_FAST, "FAST");
	if (eap == NULL)
		return -1;

	eap->init = eap_fast_init;
	eap->deinit = eap_fast_deinit;
	eap->process = eap_fast_process;
	eap->isKeyAvailable = eap_fast_isKeyAvailable;
	eap->getKey = eap_fast_getKey;
	eap->get_status = eap_fast_get_status;
#if 0
	eap->has_reauth_data = eap_fast_has_reauth_data;
	eap->deinit_for_reauth = eap_fast_deinit_for_reauth;
	eap->init_for_reauth = eap_fast_init_for_reauth;
#endif
	eap->get_emsk = eap_fast_get_emsk;

	ret = eap_peer_method_register(eap);
	if (ret)
		eap_peer_method_free(eap);
	return ret;
}
