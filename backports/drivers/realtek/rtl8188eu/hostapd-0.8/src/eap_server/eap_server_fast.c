/*
 * EAP-FAST server (RFC 4851)
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
#include "crypto/aes_wrap.h"
#include "crypto/sha1.h"
#include "crypto/tls.h"
#include "crypto/random.h"
#include "eap_common/eap_tlv_common.h"
#include "eap_common/eap_fast_common.h"
#include "eap_i.h"
#include "eap_tls_common.h"


static void eap_fast_reset(struct eap_sm *sm, void *priv);


/* Private PAC-Opaque TLV types */
#define PAC_OPAQUE_TYPE_PAD 0
#define PAC_OPAQUE_TYPE_KEY 1
#define PAC_OPAQUE_TYPE_LIFETIME 2
#define PAC_OPAQUE_TYPE_IDENTITY 3

struct eap_fast_data {
	struct eap_ssl_data ssl;
	enum {
		START, PHASE1, PHASE2_START, PHASE2_ID, PHASE2_METHOD,
		CRYPTO_BINDING, REQUEST_PAC, SUCCESS, FAILURE
	} state;

	int fast_version;
	const struct eap_method *phase2_method;
	void *phase2_priv;
	int force_version;
	int peer_version;

	u8 crypto_binding_nonce[32];
	int final_result;

	struct eap_fast_key_block_provisioning *key_block_p;

	u8 simck[EAP_FAST_SIMCK_LEN];
	u8 cmk[EAP_FAST_CMK_LEN];
	int simck_idx;

	u8 pac_opaque_encr[16];
	u8 *srv_id;
	size_t srv_id_len;
	char *srv_id_info;

	int anon_provisioning;
	int send_new_pac; /* server triggered re-keying of Tunnel PAC */
	struct wpabuf *pending_phase2_resp;
	u8 *identity; /* from PAC-Opaque */
	size_t identity_len;
	int eap_seq;
	int tnc_started;

	int pac_key_lifetime;
	int pac_key_refresh_time;
};


static int eap_fast_process_phase2_start(struct eap_sm *sm,
					 struct eap_fast_data *data);


static const char * eap_fast_state_txt(int state)
{
	switch (state) {
	case START:
		return "START";
	case PHASE1:
		return "PHASE1";
	case PHASE2_START:
		return "PHASE2_START";
	case PHASE2_ID:
		return "PHASE2_ID";
	case PHASE2_METHOD:
		return "PHASE2_METHOD";
	case CRYPTO_BINDING:
		return "CRYPTO_BINDING";
	case REQUEST_PAC:
		return "REQUEST_PAC";
	case SUCCESS:
		return "SUCCESS";
	case FAILURE:
		return "FAILURE";
	default:
		return "Unknown?!";
	}
}


static void eap_fast_state(struct eap_fast_data *data, int state)
{
	wpa_printf(MSG_DEBUG, "EAP-FAST: %s -> %s",
		   eap_fast_state_txt(data->state),
		   eap_fast_state_txt(state));
	data->state = state;
}


static EapType eap_fast_req_failure(struct eap_sm *sm,
				    struct eap_fast_data *data)
{
	/* TODO: send Result TLV(FAILURE) */
	eap_fast_state(data, FAILURE);
	return EAP_TYPE_NONE;
}


static int eap_fast_session_ticket_cb(void *ctx, const u8 *ticket, size_t len,
				      const u8 *client_random,
				      const u8 *server_random,
				      u8 *master_secret)
{
	struct eap_fast_data *data = ctx;
	const u8 *pac_opaque;
	size_t pac_opaque_len;
	u8 *buf, *pos, *end, *pac_key = NULL;
	os_time_t lifetime = 0;
	struct os_time now;
	u8 *identity = NULL;
	size_t identity_len = 0;

	wpa_printf(MSG_DEBUG, "EAP-FAST: SessionTicket callback");
	wpa_hexdump(MSG_DEBUG, "EAP-FAST: SessionTicket (PAC-Opaque)",
		    ticket, len);

	if (len < 4 || WPA_GET_BE16(ticket) != PAC_TYPE_PAC_OPAQUE) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Ignore invalid "
			   "SessionTicket");
		return 0;
	}

	pac_opaque_len = WPA_GET_BE16(ticket + 2);
	pac_opaque = ticket + 4;
	if (pac_opaque_len < 8 || pac_opaque_len % 8 ||
	    pac_opaque_len > len - 4) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Ignore invalid PAC-Opaque "
			   "(len=%lu left=%lu)",
			   (unsigned long) pac_opaque_len,
			   (unsigned long) len);
		return 0;
	}
	wpa_hexdump(MSG_DEBUG, "EAP-FAST: Received PAC-Opaque",
		    pac_opaque, pac_opaque_len);

	buf = os_malloc(pac_opaque_len - 8);
	if (buf == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Failed to allocate memory "
			   "for decrypting PAC-Opaque");
		return 0;
	}

	if (aes_unwrap(data->pac_opaque_encr, (pac_opaque_len - 8) / 8,
		       pac_opaque, buf) < 0) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Failed to decrypt "
			   "PAC-Opaque");
		os_free(buf);
		/*
		 * This may have been caused by server changing the PAC-Opaque
		 * encryption key, so just ignore this PAC-Opaque instead of
		 * failing the authentication completely. Provisioning can now
		 * be used to provision a new PAC.
		 */
		return 0;
	}

	end = buf + pac_opaque_len - 8;
	wpa_hexdump_key(MSG_DEBUG, "EAP-FAST: Decrypted PAC-Opaque",
			buf, end - buf);

	pos = buf;
	while (pos + 1 < end) {
		if (pos + 2 + pos[1] > end)
			break;

		switch (*pos) {
		case PAC_OPAQUE_TYPE_PAD:
			pos = end;
			break;
		case PAC_OPAQUE_TYPE_KEY:
			if (pos[1] != EAP_FAST_PAC_KEY_LEN) {
				wpa_printf(MSG_DEBUG, "EAP-FAST: Invalid "
					   "PAC-Key length %d", pos[1]);
				os_free(buf);
				return -1;
			}
			pac_key = pos + 2;
			wpa_hexdump_key(MSG_DEBUG, "EAP-FAST: PAC-Key from "
					"decrypted PAC-Opaque",
					pac_key, EAP_FAST_PAC_KEY_LEN);
			break;
		case PAC_OPAQUE_TYPE_LIFETIME:
			if (pos[1] != 4) {
				wpa_printf(MSG_DEBUG, "EAP-FAST: Invalid "
					   "PAC-Key lifetime length %d",
					   pos[1]);
				os_free(buf);
				return -1;
			}
			lifetime = WPA_GET_BE32(pos + 2);
			break;
		case PAC_OPAQUE_TYPE_IDENTITY:
			identity = pos + 2;
			identity_len = pos[1];
			break;
		}

		pos += 2 + pos[1];
	}

	if (pac_key == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: No PAC-Key included in "
			   "PAC-Opaque");
		os_free(buf);
		return -1;
	}

	if (identity) {
		wpa_hexdump_ascii(MSG_DEBUG, "EAP-FAST: Identity from "
				  "PAC-Opaque", identity, identity_len);
		os_free(data->identity);
		data->identity = os_malloc(identity_len);
		if (data->identity) {
			os_memcpy(data->identity, identity, identity_len);
			data->identity_len = identity_len;
		}
	}

	if (os_get_time(&now) < 0 || lifetime <= 0 || now.sec > lifetime) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: PAC-Key not valid anymore "
			   "(lifetime=%ld now=%ld)", lifetime, now.sec);
		data->send_new_pac = 2;
		/*
		 * Allow PAC to be used to allow a PAC update with some level
		 * of server authentication (i.e., do not fall back to full TLS
		 * handshake since we cannot be sure that the peer would be
		 * able to validate server certificate now). However, reject
		 * the authentication since the PAC was not valid anymore. Peer
		 * can connect again with the newly provisioned PAC after this.
		 */
	} else if (lifetime - now.sec < data->pac_key_refresh_time) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: PAC-Key soft timeout; send "
			   "an update if authentication succeeds");
		data->send_new_pac = 1;
	}

	eap_fast_derive_master_secret(pac_key, server_random, client_random,
				      master_secret);

	os_free(buf);

	return 1;
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

	if (data->phase2_method->getKey == NULL)
		return 0;

	if ((key = data->phase2_method->getKey(sm, data->phase2_priv,
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


static int eap_fast_update_icmk(struct eap_sm *sm, struct eap_fast_data *data)
{
	u8 isk[32], imck[60];

	wpa_printf(MSG_DEBUG, "EAP-FAST: Deriving ICMK[%d] (S-IMCK and CMK)",
		   data->simck_idx + 1);

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
	os_memcpy(data->cmk, imck + EAP_FAST_SIMCK_LEN, EAP_FAST_CMK_LEN);
	wpa_hexdump_key(MSG_MSGDUMP, "EAP-FAST: CMK[j]",
			data->cmk, EAP_FAST_CMK_LEN);

	return 0;
}


static void * eap_fast_init(struct eap_sm *sm)
{
	struct eap_fast_data *data;
	u8 ciphers[5] = {
		TLS_CIPHER_ANON_DH_AES128_SHA,
		TLS_CIPHER_AES128_SHA,
		TLS_CIPHER_RSA_DHE_AES128_SHA,
		TLS_CIPHER_RC4_SHA,
		TLS_CIPHER_NONE
	};

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->fast_version = EAP_FAST_VERSION;
	data->force_version = -1;
	if (sm->user && sm->user->force_version >= 0) {
		data->force_version = sm->user->force_version;
		wpa_printf(MSG_DEBUG, "EAP-FAST: forcing version %d",
			   data->force_version);
		data->fast_version = data->force_version;
	}
	data->state = START;

	if (eap_server_tls_ssl_init(sm, &data->ssl, 0)) {
		wpa_printf(MSG_INFO, "EAP-FAST: Failed to initialize SSL.");
		eap_fast_reset(sm, data);
		return NULL;
	}

	if (tls_connection_set_cipher_list(sm->ssl_ctx, data->ssl.conn,
					   ciphers) < 0) {
		wpa_printf(MSG_INFO, "EAP-FAST: Failed to set TLS cipher "
			   "suites");
		eap_fast_reset(sm, data);
		return NULL;
	}

	if (tls_connection_set_session_ticket_cb(sm->ssl_ctx, data->ssl.conn,
						 eap_fast_session_ticket_cb,
						 data) < 0) {
		wpa_printf(MSG_INFO, "EAP-FAST: Failed to set SessionTicket "
			   "callback");
		eap_fast_reset(sm, data);
		return NULL;
	}

	if (sm->pac_opaque_encr_key == NULL) {
		wpa_printf(MSG_INFO, "EAP-FAST: No PAC-Opaque encryption key "
			   "configured");
		eap_fast_reset(sm, data);
		return NULL;
	}
	os_memcpy(data->pac_opaque_encr, sm->pac_opaque_encr_key,
		  sizeof(data->pac_opaque_encr));

	if (sm->eap_fast_a_id == NULL) {
		wpa_printf(MSG_INFO, "EAP-FAST: No A-ID configured");
		eap_fast_reset(sm, data);
		return NULL;
	}
	data->srv_id = os_malloc(sm->eap_fast_a_id_len);
	if (data->srv_id == NULL) {
		eap_fast_reset(sm, data);
		return NULL;
	}
	os_memcpy(data->srv_id, sm->eap_fast_a_id, sm->eap_fast_a_id_len);
	data->srv_id_len = sm->eap_fast_a_id_len;

	if (sm->eap_fast_a_id_info == NULL) {
		wpa_printf(MSG_INFO, "EAP-FAST: No A-ID-Info configured");
		eap_fast_reset(sm, data);
		return NULL;
	}
	data->srv_id_info = os_strdup(sm->eap_fast_a_id_info);
	if (data->srv_id_info == NULL) {
		eap_fast_reset(sm, data);
		return NULL;
	}

	/* PAC-Key lifetime in seconds (hard limit) */
	data->pac_key_lifetime = sm->pac_key_lifetime;

	/*
	 * PAC-Key refresh time in seconds (soft limit on remaining hard
	 * limit). The server will generate a new PAC-Key when this number of
	 * seconds (or fewer) of the lifetime remains.
	 */
	data->pac_key_refresh_time = sm->pac_key_refresh_time;

	return data;
}


static void eap_fast_reset(struct eap_sm *sm, void *priv)
{
	struct eap_fast_data *data = priv;
	if (data == NULL)
		return;
	if (data->phase2_priv && data->phase2_method)
		data->phase2_method->reset(sm, data->phase2_priv);
	eap_server_tls_ssl_deinit(sm, &data->ssl);
	os_free(data->srv_id);
	os_free(data->srv_id_info);
	os_free(data->key_block_p);
	wpabuf_free(data->pending_phase2_resp);
	os_free(data->identity);
	os_free(data);
}


static struct wpabuf * eap_fast_build_start(struct eap_sm *sm,
					    struct eap_fast_data *data, u8 id)
{
	struct wpabuf *req;

	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_FAST,
			    1 + sizeof(struct pac_tlv_hdr) + data->srv_id_len,
			    EAP_CODE_REQUEST, id);
	if (req == NULL) {
		wpa_printf(MSG_ERROR, "EAP-FAST: Failed to allocate memory for"
			   " request");
		eap_fast_state(data, FAILURE);
		return NULL;
	}

	wpabuf_put_u8(req, EAP_TLS_FLAGS_START | data->fast_version);

	/* RFC 4851, 4.1.1. Authority ID Data */
	eap_fast_put_tlv(req, PAC_TYPE_A_ID, data->srv_id, data->srv_id_len);

	eap_fast_state(data, PHASE1);

	return req;
}


static int eap_fast_phase1_done(struct eap_sm *sm, struct eap_fast_data *data)
{
	char cipher[64];

	wpa_printf(MSG_DEBUG, "EAP-FAST: Phase1 done, starting Phase2");

	if (tls_get_cipher(sm->ssl_ctx, data->ssl.conn, cipher, sizeof(cipher))
	    < 0) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Failed to get cipher "
			   "information");
		eap_fast_state(data, FAILURE);
		return -1;
	}
	data->anon_provisioning = os_strstr(cipher, "ADH") != NULL;
		    
	if (data->anon_provisioning) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Anonymous provisioning");
		eap_fast_derive_key_provisioning(sm, data);
	} else
		eap_fast_derive_key_auth(sm, data);

	eap_fast_state(data, PHASE2_START);

	return 0;
}


static struct wpabuf * eap_fast_build_phase2_req(struct eap_sm *sm,
						 struct eap_fast_data *data,
						 u8 id)
{
	struct wpabuf *req;

	if (data->phase2_priv == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Phase 2 method not "
			   "initialized");
		return NULL;
	}
	req = data->phase2_method->buildReq(sm, data->phase2_priv, id);
	if (req == NULL)
		return NULL;

	wpa_hexdump_buf_key(MSG_MSGDUMP, "EAP-FAST: Phase 2 EAP-Request", req);
	return eap_fast_tlv_eap_payload(req);
}


static struct wpabuf * eap_fast_build_crypto_binding(
	struct eap_sm *sm, struct eap_fast_data *data)
{
	struct wpabuf *buf;
	struct eap_tlv_result_tlv *result;
	struct eap_tlv_crypto_binding_tlv *binding;

	buf = wpabuf_alloc(2 * sizeof(*result) + sizeof(*binding));
	if (buf == NULL)
		return NULL;

	if (data->send_new_pac || data->anon_provisioning ||
	    data->phase2_method)
		data->final_result = 0;
	else
		data->final_result = 1;

	if (!data->final_result || data->eap_seq > 1) {
		/* Intermediate-Result */
		wpa_printf(MSG_DEBUG, "EAP-FAST: Add Intermediate-Result TLV "
			   "(status=SUCCESS)");
		result = wpabuf_put(buf, sizeof(*result));
		result->tlv_type = host_to_be16(
			EAP_TLV_TYPE_MANDATORY |
			EAP_TLV_INTERMEDIATE_RESULT_TLV);
		result->length = host_to_be16(2);
		result->status = host_to_be16(EAP_TLV_RESULT_SUCCESS);
	}

	if (data->final_result) {
		/* Result TLV */
		wpa_printf(MSG_DEBUG, "EAP-FAST: Add Result TLV "
			   "(status=SUCCESS)");
		result = wpabuf_put(buf, sizeof(*result));
		result->tlv_type = host_to_be16(EAP_TLV_TYPE_MANDATORY |
						EAP_TLV_RESULT_TLV);
		result->length = host_to_be16(2);
		result->status = host_to_be16(EAP_TLV_RESULT_SUCCESS);
	}

	/* Crypto-Binding TLV */
	binding = wpabuf_put(buf, sizeof(*binding));
	binding->tlv_type = host_to_be16(EAP_TLV_TYPE_MANDATORY |
					 EAP_TLV_CRYPTO_BINDING_TLV);
	binding->length = host_to_be16(sizeof(*binding) -
				       sizeof(struct eap_tlv_hdr));
	binding->version = EAP_FAST_VERSION;
	binding->received_version = data->peer_version;
	binding->subtype = EAP_TLV_CRYPTO_BINDING_SUBTYPE_REQUEST;
	if (random_get_bytes(binding->nonce, sizeof(binding->nonce)) < 0) {
		wpabuf_free(buf);
		return NULL;
	}

	/*
	 * RFC 4851, Section 4.2.8:
	 * The nonce in a request MUST have its least significant bit set to 0.
	 */
	binding->nonce[sizeof(binding->nonce) - 1] &= ~0x01;

	os_memcpy(data->crypto_binding_nonce, binding->nonce,
		  sizeof(binding->nonce));

	/*
	 * RFC 4851, Section 5.3:
	 * CMK = CMK[j]
	 * Compound-MAC = HMAC-SHA1( CMK, Crypto-Binding TLV )
	 */

	hmac_sha1(data->cmk, EAP_FAST_CMK_LEN,
		  (u8 *) binding, sizeof(*binding),
		  binding->compound_mac);

	wpa_printf(MSG_DEBUG, "EAP-FAST: Add Crypto-Binding TLV: Version %d "
		   "Received Version %d SubType %d",
		   binding->version, binding->received_version,
		   binding->subtype);
	wpa_hexdump(MSG_MSGDUMP, "EAP-FAST: NONCE",
		    binding->nonce, sizeof(binding->nonce));
	wpa_hexdump(MSG_MSGDUMP, "EAP-FAST: Compound MAC",
		    binding->compound_mac, sizeof(binding->compound_mac));

	return buf;
}


static struct wpabuf * eap_fast_build_pac(struct eap_sm *sm,
					  struct eap_fast_data *data)
{
	u8 pac_key[EAP_FAST_PAC_KEY_LEN];
	u8 *pac_buf, *pac_opaque;
	struct wpabuf *buf;
	u8 *pos;
	size_t buf_len, srv_id_info_len, pac_len;
	struct eap_tlv_hdr *pac_tlv;
	struct pac_tlv_hdr *pac_info;
	struct eap_tlv_result_tlv *result;
	struct os_time now;

	if (random_get_bytes(pac_key, EAP_FAST_PAC_KEY_LEN) < 0 ||
	    os_get_time(&now) < 0)
		return NULL;
	wpa_hexdump_key(MSG_DEBUG, "EAP-FAST: Generated PAC-Key",
			pac_key, EAP_FAST_PAC_KEY_LEN);

	pac_len = (2 + EAP_FAST_PAC_KEY_LEN) + (2 + 4) +
		(2 + sm->identity_len) + 8;
	pac_buf = os_malloc(pac_len);
	if (pac_buf == NULL)
		return NULL;

	srv_id_info_len = os_strlen(data->srv_id_info);

	pos = pac_buf;
	*pos++ = PAC_OPAQUE_TYPE_KEY;
	*pos++ = EAP_FAST_PAC_KEY_LEN;
	os_memcpy(pos, pac_key, EAP_FAST_PAC_KEY_LEN);
	pos += EAP_FAST_PAC_KEY_LEN;

	*pos++ = PAC_OPAQUE_TYPE_LIFETIME;
	*pos++ = 4;
	WPA_PUT_BE32(pos, now.sec + data->pac_key_lifetime);
	pos += 4;

	if (sm->identity) {
		*pos++ = PAC_OPAQUE_TYPE_IDENTITY;
		*pos++ = sm->identity_len;
		os_memcpy(pos, sm->identity, sm->identity_len);
		pos += sm->identity_len;
	}

	pac_len = pos - pac_buf;
	while (pac_len % 8) {
		*pos++ = PAC_OPAQUE_TYPE_PAD;
		pac_len++;
	}

	pac_opaque = os_malloc(pac_len + 8);
	if (pac_opaque == NULL) {
		os_free(pac_buf);
		return NULL;
	}
	if (aes_wrap(data->pac_opaque_encr, pac_len / 8, pac_buf,
		     pac_opaque) < 0) {
		os_free(pac_buf);
		os_free(pac_opaque);
		return NULL;
	}
	os_free(pac_buf);

	pac_len += 8;
	wpa_hexdump(MSG_DEBUG, "EAP-FAST: PAC-Opaque",
		    pac_opaque, pac_len);

	buf_len = sizeof(*pac_tlv) +
		sizeof(struct pac_tlv_hdr) + EAP_FAST_PAC_KEY_LEN +
		sizeof(struct pac_tlv_hdr) + pac_len +
		data->srv_id_len + srv_id_info_len + 100 + sizeof(*result);
	buf = wpabuf_alloc(buf_len);
	if (buf == NULL) {
		os_free(pac_opaque);
		return NULL;
	}

	/* Result TLV */
	wpa_printf(MSG_DEBUG, "EAP-FAST: Add Result TLV (status=SUCCESS)");
	result = wpabuf_put(buf, sizeof(*result));
	WPA_PUT_BE16((u8 *) &result->tlv_type,
		     EAP_TLV_TYPE_MANDATORY | EAP_TLV_RESULT_TLV);
	WPA_PUT_BE16((u8 *) &result->length, 2);
	WPA_PUT_BE16((u8 *) &result->status, EAP_TLV_RESULT_SUCCESS);

	/* PAC TLV */
	wpa_printf(MSG_DEBUG, "EAP-FAST: Add PAC TLV");
	pac_tlv = wpabuf_put(buf, sizeof(*pac_tlv));
	pac_tlv->tlv_type = host_to_be16(EAP_TLV_TYPE_MANDATORY |
					 EAP_TLV_PAC_TLV);

	/* PAC-Key */
	eap_fast_put_tlv(buf, PAC_TYPE_PAC_KEY, pac_key, EAP_FAST_PAC_KEY_LEN);

	/* PAC-Opaque */
	eap_fast_put_tlv(buf, PAC_TYPE_PAC_OPAQUE, pac_opaque, pac_len);
	os_free(pac_opaque);

	/* PAC-Info */
	pac_info = wpabuf_put(buf, sizeof(*pac_info));
	pac_info->type = host_to_be16(PAC_TYPE_PAC_INFO);

	/* PAC-Lifetime (inside PAC-Info) */
	eap_fast_put_tlv_hdr(buf, PAC_TYPE_CRED_LIFETIME, 4);
	wpabuf_put_be32(buf, now.sec + data->pac_key_lifetime);

	/* A-ID (inside PAC-Info) */
	eap_fast_put_tlv(buf, PAC_TYPE_A_ID, data->srv_id, data->srv_id_len);
	
	/* Note: headers may be misaligned after A-ID */

	if (sm->identity) {
		eap_fast_put_tlv(buf, PAC_TYPE_I_ID, sm->identity,
				 sm->identity_len);
	}

	/* A-ID-Info (inside PAC-Info) */
	eap_fast_put_tlv(buf, PAC_TYPE_A_ID_INFO, data->srv_id_info,
			 srv_id_info_len);

	/* PAC-Type (inside PAC-Info) */
	eap_fast_put_tlv_hdr(buf, PAC_TYPE_PAC_TYPE, 2);
	wpabuf_put_be16(buf, PAC_TYPE_TUNNEL_PAC);

	/* Update PAC-Info and PAC TLV Length fields */
	pos = wpabuf_put(buf, 0);
	pac_info->len = host_to_be16(pos - (u8 *) (pac_info + 1));
	pac_tlv->length = host_to_be16(pos - (u8 *) (pac_tlv + 1));

	return buf;
}


static int eap_fast_encrypt_phase2(struct eap_sm *sm,
				   struct eap_fast_data *data,
				   struct wpabuf *plain, int piggyback)
{
	struct wpabuf *encr;

	wpa_hexdump_buf_key(MSG_DEBUG, "EAP-FAST: Encrypting Phase 2 TLVs",
			    plain);
	encr = eap_server_tls_encrypt(sm, &data->ssl, plain);
	wpabuf_free(plain);

	if (data->ssl.tls_out && piggyback) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Piggyback Phase 2 data "
			   "(len=%d) with last Phase 1 Message (len=%d "
			   "used=%d)",
			   (int) wpabuf_len(encr),
			   (int) wpabuf_len(data->ssl.tls_out),
			   (int) data->ssl.tls_out_pos);
		if (wpabuf_resize(&data->ssl.tls_out, wpabuf_len(encr)) < 0) {
			wpa_printf(MSG_WARNING, "EAP-FAST: Failed to resize "
				   "output buffer");
			wpabuf_free(encr);
			return -1;
		}
		wpabuf_put_buf(data->ssl.tls_out, encr);
		wpabuf_free(encr);
	} else {
		wpabuf_free(data->ssl.tls_out);
		data->ssl.tls_out_pos = 0;
		data->ssl.tls_out = encr;
	}

	return 0;
}


static struct wpabuf * eap_fast_buildReq(struct eap_sm *sm, void *priv, u8 id)
{
	struct eap_fast_data *data = priv;
	struct wpabuf *req = NULL;
	int piggyback = 0;

	if (data->ssl.state == FRAG_ACK) {
		return eap_server_tls_build_ack(id, EAP_TYPE_FAST,
						data->fast_version);
	}

	if (data->ssl.state == WAIT_FRAG_ACK) {
		return eap_server_tls_build_msg(&data->ssl, EAP_TYPE_FAST,
						data->fast_version, id);
	}

	switch (data->state) {
	case START:
		return eap_fast_build_start(sm, data, id);
	case PHASE1:
		if (tls_connection_established(sm->ssl_ctx, data->ssl.conn)) {
			if (eap_fast_phase1_done(sm, data) < 0)
				return NULL;
			if (data->state == PHASE2_START) {
				/*
				 * Try to generate Phase 2 data to piggyback
				 * with the end of Phase 1 to avoid extra
				 * roundtrip.
				 */
				wpa_printf(MSG_DEBUG, "EAP-FAST: Try to start "
					   "Phase 2");
				if (eap_fast_process_phase2_start(sm, data))
					break;
				req = eap_fast_build_phase2_req(sm, data, id);
				piggyback = 1;
			}
		}
		break;
	case PHASE2_ID:
	case PHASE2_METHOD:
		req = eap_fast_build_phase2_req(sm, data, id);
		break;
	case CRYPTO_BINDING:
		req = eap_fast_build_crypto_binding(sm, data);
		if (data->phase2_method) {
			/*
			 * Include the start of the next EAP method in the
			 * sequence in the same message with Crypto-Binding to
			 * save a round-trip.
			 */
			struct wpabuf *eap;
			eap = eap_fast_build_phase2_req(sm, data, id);
			req = wpabuf_concat(req, eap);
			eap_fast_state(data, PHASE2_METHOD);
		}
		break;
	case REQUEST_PAC:
		req = eap_fast_build_pac(sm, data);
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-FAST: %s - unexpected state %d",
			   __func__, data->state);
		return NULL;
	}

	if (req &&
	    eap_fast_encrypt_phase2(sm, data, req, piggyback) < 0)
		return NULL;

	return eap_server_tls_build_msg(&data->ssl, EAP_TYPE_FAST,
					data->fast_version, id);
}


static Boolean eap_fast_check(struct eap_sm *sm, void *priv,
			      struct wpabuf *respData)
{
	const u8 *pos;
	size_t len;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_FAST, respData, &len);
	if (pos == NULL || len < 1) {
		wpa_printf(MSG_INFO, "EAP-FAST: Invalid frame");
		return TRUE;
	}

	return FALSE;
}


static int eap_fast_phase2_init(struct eap_sm *sm, struct eap_fast_data *data,
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


static void eap_fast_process_phase2_response(struct eap_sm *sm,
					     struct eap_fast_data *data,
					     u8 *in_data, size_t in_len)
{
	u8 next_type = EAP_TYPE_NONE;
	struct eap_hdr *hdr;
	u8 *pos;
	size_t left;
	struct wpabuf buf;
	const struct eap_method *m = data->phase2_method;
	void *priv = data->phase2_priv;

	if (priv == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: %s - Phase2 not "
			   "initialized?!", __func__);
		return;
	}

	hdr = (struct eap_hdr *) in_data;
	pos = (u8 *) (hdr + 1);

	if (in_len > sizeof(*hdr) && *pos == EAP_TYPE_NAK) {
		left = in_len - sizeof(*hdr);
		wpa_hexdump(MSG_DEBUG, "EAP-FAST: Phase2 type Nak'ed; "
			    "allowed types", pos + 1, left - 1);
#ifdef EAP_SERVER_TNC
		if (m && m->vendor == EAP_VENDOR_IETF &&
		    m->method == EAP_TYPE_TNC) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: Peer Nak'ed required "
				   "TNC negotiation");
			next_type = eap_fast_req_failure(sm, data);
			eap_fast_phase2_init(sm, data, next_type);
			return;
		}
#endif /* EAP_SERVER_TNC */
		eap_sm_process_nak(sm, pos + 1, left - 1);
		if (sm->user && sm->user_eap_method_index < EAP_MAX_METHODS &&
		    sm->user->methods[sm->user_eap_method_index].method !=
		    EAP_TYPE_NONE) {
			next_type = sm->user->methods[
				sm->user_eap_method_index++].method;
			wpa_printf(MSG_DEBUG, "EAP-FAST: try EAP type %d",
				   next_type);
		} else {
			next_type = eap_fast_req_failure(sm, data);
		}
		eap_fast_phase2_init(sm, data, next_type);
		return;
	}

	wpabuf_set(&buf, in_data, in_len);

	if (m->check(sm, priv, &buf)) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Phase2 check() asked to "
			   "ignore the packet");
		next_type = eap_fast_req_failure(sm, data);
		return;
	}

	m->process(sm, priv, &buf);

	if (!m->isDone(sm, priv))
		return;

	if (!m->isSuccess(sm, priv)) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Phase2 method failed");
		next_type = eap_fast_req_failure(sm, data);
		eap_fast_phase2_init(sm, data, next_type);
		return;
	}

	switch (data->state) {
	case PHASE2_ID:
		if (eap_user_get(sm, sm->identity, sm->identity_len, 1) != 0) {
			wpa_hexdump_ascii(MSG_DEBUG, "EAP-FAST: Phase2 "
					  "Identity not found in the user "
					  "database",
					  sm->identity, sm->identity_len);
			next_type = eap_fast_req_failure(sm, data);
			break;
		}

		eap_fast_state(data, PHASE2_METHOD);
		if (data->anon_provisioning) {
			/*
			 * Only EAP-MSCHAPv2 is allowed for anonymous
			 * provisioning.
			 */
			next_type = EAP_TYPE_MSCHAPV2;
			sm->user_eap_method_index = 0;
		} else {
			next_type = sm->user->methods[0].method;
			sm->user_eap_method_index = 1;
		}
		wpa_printf(MSG_DEBUG, "EAP-FAST: try EAP type %d", next_type);
		break;
	case PHASE2_METHOD:
	case CRYPTO_BINDING:
		eap_fast_update_icmk(sm, data);
		eap_fast_state(data, CRYPTO_BINDING);
		data->eap_seq++;
		next_type = EAP_TYPE_NONE;
#ifdef EAP_SERVER_TNC
		if (sm->tnc && !data->tnc_started) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: Initialize TNC");
			next_type = EAP_TYPE_TNC;
			data->tnc_started = 1;
		}
#endif /* EAP_SERVER_TNC */
		break;
	case FAILURE:
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-FAST: %s - unexpected state %d",
			   __func__, data->state);
		break;
	}

	eap_fast_phase2_init(sm, data, next_type);
}


static void eap_fast_process_phase2_eap(struct eap_sm *sm,
					struct eap_fast_data *data,
					u8 *in_data, size_t in_len)
{
	struct eap_hdr *hdr;
	size_t len;

	hdr = (struct eap_hdr *) in_data;
	if (in_len < (int) sizeof(*hdr)) {
		wpa_printf(MSG_INFO, "EAP-FAST: Too short Phase 2 "
			   "EAP frame (len=%lu)", (unsigned long) in_len);
		eap_fast_req_failure(sm, data);
		return;
	}
	len = be_to_host16(hdr->length);
	if (len > in_len) {
		wpa_printf(MSG_INFO, "EAP-FAST: Length mismatch in "
			   "Phase 2 EAP frame (len=%lu hdr->length=%lu)",
			   (unsigned long) in_len, (unsigned long) len);
		eap_fast_req_failure(sm, data);
		return;
	}
	wpa_printf(MSG_DEBUG, "EAP-FAST: Received Phase 2: code=%d "
		   "identifier=%d length=%lu", hdr->code, hdr->identifier,
		   (unsigned long) len);
	switch (hdr->code) {
	case EAP_CODE_RESPONSE:
		eap_fast_process_phase2_response(sm, data, (u8 *) hdr, len);
		break;
	default:
		wpa_printf(MSG_INFO, "EAP-FAST: Unexpected code=%d in "
			   "Phase 2 EAP header", hdr->code);
		break;
	}
}


static int eap_fast_parse_tlvs(struct wpabuf *data,
			       struct eap_fast_tlv_parse *tlv)
{
	int mandatory, tlv_type, len, res;
	u8 *pos, *end;

	os_memset(tlv, 0, sizeof(*tlv));

	pos = wpabuf_mhead(data);
	end = pos + wpabuf_len(data);
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
				/* TODO: generate Nak TLV */
				break;
			} else {
				wpa_printf(MSG_DEBUG, "EAP-FAST: Ignored "
					   "unknown optional TLV type %d",
					   tlv_type);
			}
		}

		pos += len;
	}

	return 0;
}


static int eap_fast_validate_crypto_binding(
	struct eap_fast_data *data, struct eap_tlv_crypto_binding_tlv *b,
	size_t bind_len)
{
	u8 cmac[SHA1_MAC_LEN];

	wpa_printf(MSG_DEBUG, "EAP-FAST: Reply Crypto-Binding TLV: "
		   "Version %d Received Version %d SubType %d",
		   b->version, b->received_version, b->subtype);
	wpa_hexdump(MSG_MSGDUMP, "EAP-FAST: NONCE",
		    b->nonce, sizeof(b->nonce));
	wpa_hexdump(MSG_MSGDUMP, "EAP-FAST: Compound MAC",
		    b->compound_mac, sizeof(b->compound_mac));

	if (b->version != EAP_FAST_VERSION ||
	    b->received_version != EAP_FAST_VERSION) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Unexpected version "
			   "in Crypto-Binding: version %d "
			   "received_version %d", b->version,
			   b->received_version);
		return -1;
	}

	if (b->subtype != EAP_TLV_CRYPTO_BINDING_SUBTYPE_RESPONSE) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Unexpected subtype in "
			   "Crypto-Binding: %d", b->subtype);
		return -1;
	}

	if (os_memcmp(data->crypto_binding_nonce, b->nonce, 31) != 0 ||
	    (data->crypto_binding_nonce[31] | 1) != b->nonce[31]) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Invalid nonce in "
			   "Crypto-Binding");
		return -1;
	}

	os_memcpy(cmac, b->compound_mac, sizeof(cmac));
	os_memset(b->compound_mac, 0, sizeof(cmac));
	wpa_hexdump(MSG_MSGDUMP, "EAP-FAST: Crypto-Binding TLV for "
		    "Compound MAC calculation",
		    (u8 *) b, bind_len);
	hmac_sha1(data->cmk, EAP_FAST_CMK_LEN, (u8 *) b, bind_len,
		  b->compound_mac);
	if (os_memcmp(cmac, b->compound_mac, sizeof(cmac)) != 0) {
		wpa_hexdump(MSG_MSGDUMP,
			    "EAP-FAST: Calculated Compound MAC",
			    b->compound_mac, sizeof(cmac));
		wpa_printf(MSG_INFO, "EAP-FAST: Compound MAC did not "
			   "match");
		return -1;
	}

	return 0;
}


static int eap_fast_pac_type(u8 *pac, size_t len, u16 type)
{
	struct eap_tlv_pac_type_tlv *tlv;

	if (pac == NULL || len != sizeof(*tlv))
		return 0;

	tlv = (struct eap_tlv_pac_type_tlv *) pac;

	return be_to_host16(tlv->tlv_type) == PAC_TYPE_PAC_TYPE &&
		be_to_host16(tlv->length) == 2 &&
		be_to_host16(tlv->pac_type) == type;
}


static void eap_fast_process_phase2_tlvs(struct eap_sm *sm,
					 struct eap_fast_data *data,
					 struct wpabuf *in_data)
{
	struct eap_fast_tlv_parse tlv;
	int check_crypto_binding = data->state == CRYPTO_BINDING;

	if (eap_fast_parse_tlvs(in_data, &tlv) < 0) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Failed to parse received "
			   "Phase 2 TLVs");
		return;
	}

	if (tlv.result == EAP_TLV_RESULT_FAILURE) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Result TLV indicated "
			   "failure");
		eap_fast_state(data, FAILURE);
		return;
	}

	if (data->state == REQUEST_PAC) {
		u16 type, len, res;
		if (tlv.pac == NULL || tlv.pac_len < 6) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: No PAC "
				   "Acknowledgement received");
			eap_fast_state(data, FAILURE);
			return;
		}

		type = WPA_GET_BE16(tlv.pac);
		len = WPA_GET_BE16(tlv.pac + 2);
		res = WPA_GET_BE16(tlv.pac + 4);

		if (type != PAC_TYPE_PAC_ACKNOWLEDGEMENT || len != 2 ||
		    res != EAP_TLV_RESULT_SUCCESS) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: PAC TLV did not "
				   "contain acknowledgement");
			eap_fast_state(data, FAILURE);
			return;
		}

		wpa_printf(MSG_DEBUG, "EAP-FAST: PAC-Acknowledgement received "
			   "- PAC provisioning succeeded");
		eap_fast_state(data, (data->anon_provisioning ||
				      data->send_new_pac == 2) ?
			       FAILURE : SUCCESS);
		return;
	}

	if (check_crypto_binding) {
		if (tlv.crypto_binding == NULL) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: No Crypto-Binding "
				   "TLV received");
			eap_fast_state(data, FAILURE);
			return;
		}

		if (data->final_result &&
		    tlv.result != EAP_TLV_RESULT_SUCCESS) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: Crypto-Binding TLV "
				   "without Success Result");
			eap_fast_state(data, FAILURE);
			return;
		}

		if (!data->final_result &&
		    tlv.iresult != EAP_TLV_RESULT_SUCCESS) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: Crypto-Binding TLV "
				   "without intermediate Success Result");
			eap_fast_state(data, FAILURE);
			return;
		}

		if (eap_fast_validate_crypto_binding(data, tlv.crypto_binding,
						     tlv.crypto_binding_len)) {
			eap_fast_state(data, FAILURE);
			return;
		}

		wpa_printf(MSG_DEBUG, "EAP-FAST: Valid Crypto-Binding TLV "
			   "received");
		if (data->final_result) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: Authentication "
				   "completed successfully");
		}

		if (data->anon_provisioning &&
		    sm->eap_fast_prov != ANON_PROV &&
		    sm->eap_fast_prov != BOTH_PROV) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: Client is trying to "
				   "use unauthenticated provisioning which is "
				   "disabled");
			eap_fast_state(data, FAILURE);
			return;
		}

		if (sm->eap_fast_prov != AUTH_PROV &&
		    sm->eap_fast_prov != BOTH_PROV &&
		    tlv.request_action == EAP_TLV_ACTION_PROCESS_TLV &&
		    eap_fast_pac_type(tlv.pac, tlv.pac_len,
				      PAC_TYPE_TUNNEL_PAC)) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: Client is trying to "
				   "use authenticated provisioning which is "
				   "disabled");
			eap_fast_state(data, FAILURE);
			return;
		}

		if (data->anon_provisioning ||
		    (tlv.request_action == EAP_TLV_ACTION_PROCESS_TLV &&
		     eap_fast_pac_type(tlv.pac, tlv.pac_len,
				       PAC_TYPE_TUNNEL_PAC))) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: Requested a new "
				   "Tunnel PAC");
			eap_fast_state(data, REQUEST_PAC);
		} else if (data->send_new_pac) {
			wpa_printf(MSG_DEBUG, "EAP-FAST: Server triggered "
				   "re-keying of Tunnel PAC");
			eap_fast_state(data, REQUEST_PAC);
		} else if (data->final_result)
			eap_fast_state(data, SUCCESS);
	}

	if (tlv.eap_payload_tlv) {
		eap_fast_process_phase2_eap(sm, data, tlv.eap_payload_tlv,
					    tlv.eap_payload_tlv_len);
	}
}


static void eap_fast_process_phase2(struct eap_sm *sm,
				    struct eap_fast_data *data,
				    struct wpabuf *in_buf)
{
	struct wpabuf *in_decrypted;

	wpa_printf(MSG_DEBUG, "EAP-FAST: Received %lu bytes encrypted data for"
		   " Phase 2", (unsigned long) wpabuf_len(in_buf));

	if (data->pending_phase2_resp) {
		wpa_printf(MSG_DEBUG, "EAP-PEAP: Pending Phase 2 response - "
			   "skip decryption and use old data");
		eap_fast_process_phase2_tlvs(sm, data,
					     data->pending_phase2_resp);
		wpabuf_free(data->pending_phase2_resp);
		data->pending_phase2_resp = NULL;
		return;
	}

	in_decrypted = tls_connection_decrypt(sm->ssl_ctx, data->ssl.conn,
					      in_buf);
	if (in_decrypted == NULL) {
		wpa_printf(MSG_INFO, "EAP-FAST: Failed to decrypt Phase 2 "
			   "data");
		eap_fast_state(data, FAILURE);
		return;
	}

	wpa_hexdump_buf_key(MSG_DEBUG, "EAP-FAST: Decrypted Phase 2 TLVs",
			    in_decrypted);

	eap_fast_process_phase2_tlvs(sm, data, in_decrypted);

	if (sm->method_pending == METHOD_PENDING_WAIT) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: Phase2 method is in "
			   "pending wait state - save decrypted response");
		wpabuf_free(data->pending_phase2_resp);
		data->pending_phase2_resp = in_decrypted;
		return;
	}

	wpabuf_free(in_decrypted);
}


static int eap_fast_process_version(struct eap_sm *sm, void *priv,
				    int peer_version)
{
	struct eap_fast_data *data = priv;

	data->peer_version = peer_version;

	if (data->force_version >= 0 && peer_version != data->force_version) {
		wpa_printf(MSG_INFO, "EAP-FAST: peer did not select the forced"
			   " version (forced=%d peer=%d) - reject",
			   data->force_version, peer_version);
		return -1;
	}

	if (peer_version < data->fast_version) {
		wpa_printf(MSG_DEBUG, "EAP-FAST: peer ver=%d, own ver=%d; "
			   "use version %d",
			   peer_version, data->fast_version, peer_version);
		data->fast_version = peer_version;
	}

	return 0;
}


static int eap_fast_process_phase1(struct eap_sm *sm,
				   struct eap_fast_data *data)
{
	if (eap_server_tls_phase1(sm, &data->ssl) < 0) {
		wpa_printf(MSG_INFO, "EAP-FAST: TLS processing failed");
		eap_fast_state(data, FAILURE);
		return -1;
	}

	if (!tls_connection_established(sm->ssl_ctx, data->ssl.conn) ||
	    wpabuf_len(data->ssl.tls_out) > 0)
		return 1;

	/*
	 * Phase 1 was completed with the received message (e.g., when using
	 * abbreviated handshake), so Phase 2 can be started immediately
	 * without having to send through an empty message to the peer.
	 */

	return eap_fast_phase1_done(sm, data);
}


static int eap_fast_process_phase2_start(struct eap_sm *sm,
					 struct eap_fast_data *data)
{
	u8 next_type;

	if (data->identity) {
		os_free(sm->identity);
		sm->identity = data->identity;
		data->identity = NULL;
		sm->identity_len = data->identity_len;
		data->identity_len = 0;
		sm->require_identity_match = 1;
		if (eap_user_get(sm, sm->identity, sm->identity_len, 1) != 0) {
			wpa_hexdump_ascii(MSG_DEBUG, "EAP-FAST: "
					  "Phase2 Identity not found "
					  "in the user database",
					  sm->identity, sm->identity_len);
			next_type = eap_fast_req_failure(sm, data);
		} else {
			wpa_printf(MSG_DEBUG, "EAP-FAST: Identity already "
				   "known - skip Phase 2 Identity Request");
			next_type = sm->user->methods[0].method;
			sm->user_eap_method_index = 1;
		}

		eap_fast_state(data, PHASE2_METHOD);
	} else {
		eap_fast_state(data, PHASE2_ID);
		next_type = EAP_TYPE_IDENTITY;
	}

	return eap_fast_phase2_init(sm, data, next_type);
}


static void eap_fast_process_msg(struct eap_sm *sm, void *priv,
				 const struct wpabuf *respData)
{
	struct eap_fast_data *data = priv;

	switch (data->state) {
	case PHASE1:
		if (eap_fast_process_phase1(sm, data))
			break;

		/* fall through to PHASE2_START */
	case PHASE2_START:
		eap_fast_process_phase2_start(sm, data);
		break;
	case PHASE2_ID:
	case PHASE2_METHOD:
	case CRYPTO_BINDING:
	case REQUEST_PAC:
		eap_fast_process_phase2(sm, data, data->ssl.tls_in);
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-FAST: Unexpected state %d in %s",
			   data->state, __func__);
		break;
	}
}


static void eap_fast_process(struct eap_sm *sm, void *priv,
			     struct wpabuf *respData)
{
	struct eap_fast_data *data = priv;
	if (eap_server_tls_process(sm, &data->ssl, respData, data,
				   EAP_TYPE_FAST, eap_fast_process_version,
				   eap_fast_process_msg) < 0)
		eap_fast_state(data, FAILURE);
}


static Boolean eap_fast_isDone(struct eap_sm *sm, void *priv)
{
	struct eap_fast_data *data = priv;
	return data->state == SUCCESS || data->state == FAILURE;
}


static u8 * eap_fast_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_fast_data *data = priv;
	u8 *eapKeyData;

	if (data->state != SUCCESS)
		return NULL;

	eapKeyData = os_malloc(EAP_FAST_KEY_LEN);
	if (eapKeyData == NULL)
		return NULL;

	eap_fast_derive_eap_msk(data->simck, eapKeyData);
	*len = EAP_FAST_KEY_LEN;

	return eapKeyData;
}


static u8 * eap_fast_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_fast_data *data = priv;
	u8 *eapKeyData;

	if (data->state != SUCCESS)
		return NULL;

	eapKeyData = os_malloc(EAP_EMSK_LEN);
	if (eapKeyData == NULL)
		return NULL;

	eap_fast_derive_eap_emsk(data->simck, eapKeyData);
	*len = EAP_EMSK_LEN;

	return eapKeyData;
}


static Boolean eap_fast_isSuccess(struct eap_sm *sm, void *priv)
{
	struct eap_fast_data *data = priv;
	return data->state == SUCCESS;
}


int eap_server_fast_register(void)
{
	struct eap_method *eap;
	int ret;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_IETF, EAP_TYPE_FAST, "FAST");
	if (eap == NULL)
		return -1;

	eap->init = eap_fast_init;
	eap->reset = eap_fast_reset;
	eap->buildReq = eap_fast_buildReq;
	eap->check = eap_fast_check;
	eap->process = eap_fast_process;
	eap->isDone = eap_fast_isDone;
	eap->getKey = eap_fast_getKey;
	eap->get_emsk = eap_fast_get_emsk;
	eap->isSuccess = eap_fast_isSuccess;

	ret = eap_server_method_register(eap);
	if (ret)
		eap_server_method_free(eap);
	return ret;
}
