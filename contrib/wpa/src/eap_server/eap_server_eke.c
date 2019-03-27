/*
 * hostapd / EAP-EKE (RFC 6124) server
 * Copyright (c) 2013, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/random.h"
#include "eap_server/eap_i.h"
#include "eap_common/eap_eke_common.h"


struct eap_eke_data {
	enum {
		IDENTITY, COMMIT, CONFIRM, FAILURE_REPORT, SUCCESS, FAILURE
	} state;
	u8 msk[EAP_MSK_LEN];
	u8 emsk[EAP_EMSK_LEN];
	u8 *peerid;
	size_t peerid_len;
	u8 peerid_type;
	u8 serverid_type;
	u8 dh_priv[EAP_EKE_MAX_DH_LEN];
	u8 key[EAP_EKE_MAX_KEY_LEN];
	struct eap_eke_session sess;
	u8 nonce_p[EAP_EKE_MAX_NONCE_LEN];
	u8 nonce_s[EAP_EKE_MAX_NONCE_LEN];
	struct wpabuf *msgs;
	int phase2;
	u32 failure_code;
};


static const char * eap_eke_state_txt(int state)
{
	switch (state) {
	case IDENTITY:
		return "IDENTITY";
	case COMMIT:
		return "COMMIT";
	case CONFIRM:
		return "CONFIRM";
	case FAILURE_REPORT:
		return "FAILURE_REPORT";
	case SUCCESS:
		return "SUCCESS";
	case FAILURE:
		return "FAILURE";
	default:
		return "?";
	}
}


static void eap_eke_state(struct eap_eke_data *data, int state)
{
	wpa_printf(MSG_DEBUG, "EAP-EKE: %s -> %s",
		   eap_eke_state_txt(data->state),
		   eap_eke_state_txt(state));
	data->state = state;
}


static void eap_eke_fail(struct eap_eke_data *data, u32 code)
{
	wpa_printf(MSG_DEBUG, "EAP-EKE: Failure - code 0x%x", code);
	data->failure_code = code;
	eap_eke_state(data, FAILURE_REPORT);
}


static void * eap_eke_init(struct eap_sm *sm)
{
	struct eap_eke_data *data;
	size_t i;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	eap_eke_state(data, IDENTITY);

	data->serverid_type = EAP_EKE_ID_OPAQUE;
	for (i = 0; i < sm->server_id_len; i++) {
		if (sm->server_id[i] == '.' &&
		    data->serverid_type == EAP_EKE_ID_OPAQUE)
			data->serverid_type = EAP_EKE_ID_FQDN;
		if (sm->server_id[i] == '@')
			data->serverid_type = EAP_EKE_ID_NAI;
	}

	data->phase2 = sm->init_phase2;

	return data;
}


static void eap_eke_reset(struct eap_sm *sm, void *priv)
{
	struct eap_eke_data *data = priv;
	eap_eke_session_clean(&data->sess);
	os_free(data->peerid);
	wpabuf_free(data->msgs);
	bin_clear_free(data, sizeof(*data));
}


static struct wpabuf * eap_eke_build_msg(struct eap_eke_data *data,
					 u8 id, size_t length, u8 eke_exch)
{
	struct wpabuf *msg;
	size_t plen;

	plen = 1 + length;

	msg = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_EKE, plen,
			    EAP_CODE_REQUEST, id);
	if (msg == NULL) {
		wpa_printf(MSG_ERROR, "EAP-EKE: Failed to allocate memory");
		return NULL;
	}

	wpabuf_put_u8(msg, eke_exch);

	return msg;
}


static int supported_proposal(const u8 *pos)
{
	if (pos[0] == EAP_EKE_DHGROUP_EKE_16 &&
	    pos[1] == EAP_EKE_ENCR_AES128_CBC &&
	    pos[2] == EAP_EKE_PRF_HMAC_SHA2_256 &&
	    pos[3] == EAP_EKE_MAC_HMAC_SHA2_256)
		return 1;

	if (pos[0] == EAP_EKE_DHGROUP_EKE_15 &&
	    pos[1] == EAP_EKE_ENCR_AES128_CBC &&
	    pos[2] == EAP_EKE_PRF_HMAC_SHA2_256 &&
	    pos[3] == EAP_EKE_MAC_HMAC_SHA2_256)
		return 1;

	if (pos[0] == EAP_EKE_DHGROUP_EKE_14 &&
	    pos[1] == EAP_EKE_ENCR_AES128_CBC &&
	    pos[2] == EAP_EKE_PRF_HMAC_SHA2_256 &&
	    pos[3] == EAP_EKE_MAC_HMAC_SHA2_256)
		return 1;

	if (pos[0] == EAP_EKE_DHGROUP_EKE_14 &&
	    pos[1] == EAP_EKE_ENCR_AES128_CBC &&
	    pos[2] == EAP_EKE_PRF_HMAC_SHA1 &&
	    pos[3] == EAP_EKE_MAC_HMAC_SHA1)
		return 1;

	return 0;
}


static struct wpabuf * eap_eke_build_failure(struct eap_eke_data *data, u8 id)
{
	struct wpabuf *msg;

	wpa_printf(MSG_DEBUG, "EAP-EKE: Request/Failure: Failure-Code=0x%x",
		   data->failure_code);

	msg = eap_eke_build_msg(data, id, 4, EAP_EKE_FAILURE);
	if (msg == NULL) {
		eap_eke_state(data, FAILURE);
		return NULL;
	}
	wpabuf_put_be32(msg, data->failure_code);

	return msg;
}


static struct wpabuf * eap_eke_build_identity(struct eap_sm *sm,
					      struct eap_eke_data *data,
					      u8 id)
{
	struct wpabuf *msg;
	size_t plen;

	wpa_printf(MSG_DEBUG, "EAP-EKE: Request/Identity");

	plen = 2 + 4 * 4 + 1 + sm->server_id_len;
	msg = eap_eke_build_msg(data, id, plen, EAP_EKE_ID);
	if (msg == NULL)
		return NULL;

	wpabuf_put_u8(msg, 4); /* NumProposals */
	wpabuf_put_u8(msg, 0); /* Reserved */

	/* Proposal - DH Group 16 with AES128-CBC and SHA256 */
	wpabuf_put_u8(msg, EAP_EKE_DHGROUP_EKE_16); /* Group Description */
	wpabuf_put_u8(msg, EAP_EKE_ENCR_AES128_CBC); /* Encryption */
	wpabuf_put_u8(msg, EAP_EKE_PRF_HMAC_SHA2_256); /* PRF */
	wpabuf_put_u8(msg, EAP_EKE_MAC_HMAC_SHA2_256); /* MAC */

	/* Proposal - DH Group 15 with AES128-CBC and SHA256 */
	wpabuf_put_u8(msg, EAP_EKE_DHGROUP_EKE_15); /* Group Description */
	wpabuf_put_u8(msg, EAP_EKE_ENCR_AES128_CBC); /* Encryption */
	wpabuf_put_u8(msg, EAP_EKE_PRF_HMAC_SHA2_256); /* PRF */
	wpabuf_put_u8(msg, EAP_EKE_MAC_HMAC_SHA2_256); /* MAC */

	/* Proposal - DH Group 14 with AES128-CBC and SHA256 */
	wpabuf_put_u8(msg, EAP_EKE_DHGROUP_EKE_14); /* Group Description */
	wpabuf_put_u8(msg, EAP_EKE_ENCR_AES128_CBC); /* Encryption */
	wpabuf_put_u8(msg, EAP_EKE_PRF_HMAC_SHA2_256); /* PRF */
	wpabuf_put_u8(msg, EAP_EKE_MAC_HMAC_SHA2_256); /* MAC */

	/*
	 * Proposal - DH Group 14 with AES128-CBC and SHA1
	 * (mandatory to implement algorithms)
	 */
	wpabuf_put_u8(msg, EAP_EKE_DHGROUP_EKE_14); /* Group Description */
	wpabuf_put_u8(msg, EAP_EKE_ENCR_AES128_CBC); /* Encryption */
	wpabuf_put_u8(msg, EAP_EKE_PRF_HMAC_SHA1); /* PRF */
	wpabuf_put_u8(msg, EAP_EKE_MAC_HMAC_SHA1); /* MAC */

	/* Server IDType + Identity */
	wpabuf_put_u8(msg, data->serverid_type);
	wpabuf_put_data(msg, sm->server_id, sm->server_id_len);

	wpabuf_free(data->msgs);
	data->msgs = wpabuf_dup(msg);
	if (data->msgs == NULL) {
		wpabuf_free(msg);
		return NULL;
	}

	return msg;
}


static struct wpabuf * eap_eke_build_commit(struct eap_sm *sm,
					    struct eap_eke_data *data, u8 id)
{
	struct wpabuf *msg;
	u8 pub[EAP_EKE_MAX_DH_LEN];

	wpa_printf(MSG_DEBUG, "EAP-EKE: Request/Commit");

	if (sm->user == NULL || sm->user->password == NULL) {
		wpa_printf(MSG_INFO, "EAP-EKE: Password with not configured");
		eap_eke_fail(data, EAP_EKE_FAIL_PASSWD_NOT_FOUND);
		return eap_eke_build_failure(data, id);
	}

	if (eap_eke_derive_key(&data->sess, sm->user->password,
			       sm->user->password_len,
			       sm->server_id, sm->server_id_len,
			       data->peerid, data->peerid_len, data->key) < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to derive key");
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return eap_eke_build_failure(data, id);
	}

	msg = eap_eke_build_msg(data, id, data->sess.dhcomp_len,
				EAP_EKE_COMMIT);
	if (msg == NULL) {
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return eap_eke_build_failure(data, id);
	}

	/*
	 * y_s = g ^ x_s (mod p)
	 * x_s = random number 2 .. p-1
	 * temp = prf(0+, password)
	 * key = prf+(temp, ID_S | ID_P)
	 * DHComponent_S = Encr(key, y_s)
	 */

	if (eap_eke_dh_init(data->sess.dhgroup, data->dh_priv, pub) < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to initialize DH");
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return eap_eke_build_failure(data, id);
	}

	if (eap_eke_dhcomp(&data->sess, data->key, pub,
			   wpabuf_put(msg, data->sess.dhcomp_len))
	    < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to build DHComponent_S");
		wpabuf_free(msg);
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return eap_eke_build_failure(data, id);
	}

	if (wpabuf_resize(&data->msgs, wpabuf_len(msg)) < 0) {
		wpabuf_free(msg);
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return eap_eke_build_failure(data, id);
	}
	wpabuf_put_buf(data->msgs, msg);

	return msg;
}


static struct wpabuf * eap_eke_build_confirm(struct eap_sm *sm,
					     struct eap_eke_data *data, u8 id)
{
	struct wpabuf *msg;
	size_t plen, prot_len;
	u8 nonces[2 * EAP_EKE_MAX_NONCE_LEN];
	u8 *auth;

	wpa_printf(MSG_DEBUG, "EAP-EKE: Request/Confirm");

	plen = data->sess.pnonce_ps_len + data->sess.prf_len;
	msg = eap_eke_build_msg(data, id, plen, EAP_EKE_CONFIRM);
	if (msg == NULL) {
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return eap_eke_build_failure(data, id);
	}

	if (random_get_bytes(data->nonce_s, data->sess.nonce_len)) {
		wpabuf_free(msg);
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return eap_eke_build_failure(data, id);
	}
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: Nonce_S",
			data->nonce_s, data->sess.nonce_len);

	os_memcpy(nonces, data->nonce_p, data->sess.nonce_len);
	os_memcpy(nonces + data->sess.nonce_len, data->nonce_s,
		  data->sess.nonce_len);
	prot_len = wpabuf_tailroom(msg);
	if (eap_eke_prot(&data->sess, nonces, 2 * data->sess.nonce_len,
			 wpabuf_put(msg, 0), &prot_len) < 0) {
		wpabuf_free(msg);
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return eap_eke_build_failure(data, id);
	}
	wpabuf_put(msg, prot_len);

	if (eap_eke_derive_ka(&data->sess,
			      sm->server_id, sm->server_id_len,
			      data->peerid, data->peerid_len,
			      data->nonce_p, data->nonce_s) < 0) {
		wpabuf_free(msg);
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return eap_eke_build_failure(data, id);
	}

	auth = wpabuf_put(msg, data->sess.prf_len);
	if (eap_eke_auth(&data->sess, "EAP-EKE server", data->msgs, auth) < 0) {
		wpabuf_free(msg);
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return eap_eke_build_failure(data, id);
	}
	wpa_hexdump(MSG_DEBUG, "EAP-EKE: Auth_S", auth, data->sess.prf_len);

	return msg;
}


static struct wpabuf * eap_eke_buildReq(struct eap_sm *sm, void *priv, u8 id)
{
	struct eap_eke_data *data = priv;

	switch (data->state) {
	case IDENTITY:
		return eap_eke_build_identity(sm, data, id);
	case COMMIT:
		return eap_eke_build_commit(sm, data, id);
	case CONFIRM:
		return eap_eke_build_confirm(sm, data, id);
	case FAILURE_REPORT:
		return eap_eke_build_failure(data, id);
	default:
		wpa_printf(MSG_DEBUG, "EAP-EKE: Unknown state %d in buildReq",
			   data->state);
		break;
	}
	return NULL;
}


static Boolean eap_eke_check(struct eap_sm *sm, void *priv,
			     struct wpabuf *respData)
{
	struct eap_eke_data *data = priv;
	size_t len;
	const u8 *pos;
	u8 eke_exch;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_EKE, respData, &len);
	if (pos == NULL || len < 1) {
		wpa_printf(MSG_INFO, "EAP-EKE: Invalid frame");
		return TRUE;
	}

	eke_exch = *pos;
	wpa_printf(MSG_DEBUG, "EAP-EKE: Received frame: EKE-Exch=%d", eke_exch);

	if (data->state == IDENTITY && eke_exch == EAP_EKE_ID)
		return FALSE;

	if (data->state == COMMIT && eke_exch == EAP_EKE_COMMIT)
		return FALSE;

	if (data->state == CONFIRM && eke_exch == EAP_EKE_CONFIRM)
		return FALSE;

	if (eke_exch == EAP_EKE_FAILURE)
		return FALSE;

	wpa_printf(MSG_INFO, "EAP-EKE: Unexpected EKE-Exch=%d in state=%d",
		   eke_exch, data->state);

	return TRUE;
}


static void eap_eke_process_identity(struct eap_sm *sm,
				     struct eap_eke_data *data,
				     const struct wpabuf *respData,
				     const u8 *payload, size_t payloadlen)
{
	const u8 *pos, *end;
	int i;

	wpa_printf(MSG_DEBUG, "EAP-EKE: Received Response/Identity");

	if (data->state != IDENTITY) {
		eap_eke_fail(data, EAP_EKE_FAIL_PROTO_ERROR);
		return;
	}

	pos = payload;
	end = payload + payloadlen;

	if (pos + 2 + 4 + 1 > end) {
		wpa_printf(MSG_INFO, "EAP-EKE: Too short EAP-EKE-ID payload");
		eap_eke_fail(data, EAP_EKE_FAIL_PROTO_ERROR);
		return;
	}

	if (*pos != 1) {
		wpa_printf(MSG_INFO, "EAP-EKE: Unexpected NumProposals %d (expected 1)",
			   *pos);
		eap_eke_fail(data, EAP_EKE_FAIL_PROTO_ERROR);
		return;
	}

	pos += 2;

	if (!supported_proposal(pos)) {
		wpa_printf(MSG_INFO, "EAP-EKE: Unexpected Proposal (%u:%u:%u:%u)",
			   pos[0], pos[1], pos[2], pos[3]);
		eap_eke_fail(data, EAP_EKE_FAIL_PROTO_ERROR);
		return;
	}

	wpa_printf(MSG_DEBUG, "EAP-EKE: Selected Proposal (%u:%u:%u:%u)",
		   pos[0], pos[1], pos[2], pos[3]);
	if (eap_eke_session_init(&data->sess, pos[0], pos[1], pos[2], pos[3]) <
	    0) {
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return;
	}
	pos += 4;

	data->peerid_type = *pos++;
	os_free(data->peerid);
	data->peerid = os_memdup(pos, end - pos);
	if (data->peerid == NULL) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to allocate memory for peerid");
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return;
	}
	data->peerid_len = end - pos;
	wpa_printf(MSG_DEBUG, "EAP-EKE: Peer IDType %u", data->peerid_type);
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-EKE: Peer Identity",
			  data->peerid, data->peerid_len);

	if (eap_user_get(sm, data->peerid, data->peerid_len, data->phase2)) {
		wpa_printf(MSG_INFO, "EAP-EKE: Peer Identity not found from user database");
		eap_eke_fail(data, EAP_EKE_FAIL_PASSWD_NOT_FOUND);
		return;
	}

	for (i = 0; i < EAP_MAX_METHODS; i++) {
		if (sm->user->methods[i].vendor == EAP_VENDOR_IETF &&
		    sm->user->methods[i].method == EAP_TYPE_EKE)
			break;
	}
	if (i == EAP_MAX_METHODS) {
		wpa_printf(MSG_INFO, "EAP-EKE: Matching user entry does not allow EAP-EKE");
		eap_eke_fail(data, EAP_EKE_FAIL_PASSWD_NOT_FOUND);
		return;
	}

	if (sm->user->password == NULL || sm->user->password_len == 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: No password configured for peer");
		eap_eke_fail(data, EAP_EKE_FAIL_PASSWD_NOT_FOUND);
		return;
	}

	if (wpabuf_resize(&data->msgs, wpabuf_len(respData)) < 0) {
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return;
	}
	wpabuf_put_buf(data->msgs, respData);

	eap_eke_state(data, COMMIT);
}


static void eap_eke_process_commit(struct eap_sm *sm,
				   struct eap_eke_data *data,
				   const struct wpabuf *respData,
				   const u8 *payload, size_t payloadlen)
{
	const u8 *pos, *end, *dhcomp, *pnonce;
	size_t decrypt_len;

	wpa_printf(MSG_DEBUG, "EAP-EKE: Received Response/Commit");

	if (data->state != COMMIT) {
		eap_eke_fail(data, EAP_EKE_FAIL_PROTO_ERROR);
		return;
	}

	pos = payload;
	end = payload + payloadlen;

	if (pos + data->sess.dhcomp_len + data->sess.pnonce_len > end) {
		wpa_printf(MSG_DEBUG, "EAP-EKE: Too short EAP-EKE-Commit");
		eap_eke_fail(data, EAP_EKE_FAIL_PROTO_ERROR);
		return;
	}

	wpa_hexdump(MSG_DEBUG, "EAP-EKE: DHComponent_P",
		    pos, data->sess.dhcomp_len);
	dhcomp = pos;
	pos += data->sess.dhcomp_len;
	wpa_hexdump(MSG_DEBUG, "EAP-EKE: PNonce_P", pos, data->sess.pnonce_len);
	pnonce = pos;
	pos += data->sess.pnonce_len;
	wpa_hexdump(MSG_DEBUG, "EAP-EKE: CBValue", pos, end - pos);

	if (eap_eke_shared_secret(&data->sess, data->key, data->dh_priv, dhcomp)
	    < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to derive shared secret");
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return;
	}

	if (eap_eke_derive_ke_ki(&data->sess,
				 sm->server_id, sm->server_id_len,
				 data->peerid, data->peerid_len) < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to derive Ke/Ki");
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return;
	}

	decrypt_len = sizeof(data->nonce_p);
	if (eap_eke_decrypt_prot(&data->sess, pnonce, data->sess.pnonce_len,
				 data->nonce_p, &decrypt_len) < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to decrypt PNonce_P");
		eap_eke_fail(data, EAP_EKE_FAIL_AUTHENTICATION_FAIL);
		return;
	}
	if (decrypt_len < (size_t) data->sess.nonce_len) {
		wpa_printf(MSG_INFO, "EAP-EKE: PNonce_P protected data too short to include Nonce_P");
		eap_eke_fail(data, EAP_EKE_FAIL_AUTHENTICATION_FAIL);
		return;
	}
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: Nonce_P",
			data->nonce_p, data->sess.nonce_len);

	if (wpabuf_resize(&data->msgs, wpabuf_len(respData)) < 0) {
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return;
	}
	wpabuf_put_buf(data->msgs, respData);

	eap_eke_state(data, CONFIRM);
}


static void eap_eke_process_confirm(struct eap_sm *sm,
				    struct eap_eke_data *data,
				    const struct wpabuf *respData,
				    const u8 *payload, size_t payloadlen)
{
	size_t decrypt_len;
	u8 nonce[EAP_EKE_MAX_NONCE_LEN];
	u8 auth_p[EAP_EKE_MAX_HASH_LEN];

	wpa_printf(MSG_DEBUG, "EAP-EKE: Received Response/Confirm");

	if (data->state != CONFIRM) {
		eap_eke_fail(data, EAP_EKE_FAIL_PROTO_ERROR);
		return;
	}

	wpa_printf(MSG_DEBUG, "EAP-EKE: Received Response/Confirm");

	if (payloadlen < (size_t) data->sess.pnonce_len + data->sess.prf_len) {
		wpa_printf(MSG_DEBUG, "EAP-EKE: Too short EAP-EKE-Confirm");
		eap_eke_fail(data, EAP_EKE_FAIL_PROTO_ERROR);
		return;
	}

	decrypt_len = sizeof(nonce);
	if (eap_eke_decrypt_prot(&data->sess, payload, data->sess.pnonce_len,
				 nonce, &decrypt_len) < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to decrypt PNonce_S");
		eap_eke_fail(data, EAP_EKE_FAIL_AUTHENTICATION_FAIL);
		return;
	}
	if (decrypt_len < (size_t) data->sess.nonce_len) {
		wpa_printf(MSG_INFO, "EAP-EKE: PNonce_S protected data too short to include Nonce_S");
		eap_eke_fail(data, EAP_EKE_FAIL_AUTHENTICATION_FAIL);
		return;
	}
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: Received Nonce_S",
			nonce, data->sess.nonce_len);
	if (os_memcmp(nonce, data->nonce_s, data->sess.nonce_len) != 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Received Nonce_S does not match previously sent Nonce_S");
		eap_eke_fail(data, EAP_EKE_FAIL_AUTHENTICATION_FAIL);
		return;
	}

	if (eap_eke_auth(&data->sess, "EAP-EKE peer", data->msgs, auth_p) < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Could not derive Auth_P");
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return;
	}
	wpa_hexdump(MSG_DEBUG, "EAP-EKE: Auth_P", auth_p, data->sess.prf_len);
	if (os_memcmp_const(auth_p, payload + data->sess.pnonce_len,
			    data->sess.prf_len) != 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Auth_P does not match");
		eap_eke_fail(data, EAP_EKE_FAIL_AUTHENTICATION_FAIL);
		return;
	}

	if (eap_eke_derive_msk(&data->sess, sm->server_id, sm->server_id_len,
			       data->peerid, data->peerid_len,
			       data->nonce_s, data->nonce_p,
			       data->msk, data->emsk) < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to derive MSK/EMSK");
		eap_eke_fail(data, EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
		return;
	}

	os_memset(data->dh_priv, 0, sizeof(data->dh_priv));
	os_memset(data->key, 0, sizeof(data->key));
	eap_eke_session_clean(&data->sess);

	eap_eke_state(data, SUCCESS);
}


static void eap_eke_process_failure(struct eap_sm *sm,
				    struct eap_eke_data *data,
				    const struct wpabuf *respData,
				    const u8 *payload, size_t payloadlen)
{
	u32 code;

	wpa_printf(MSG_DEBUG, "EAP-EKE: Received Response/Failure");

	if (payloadlen < 4) {
		wpa_printf(MSG_DEBUG, "EAP-EKE: Too short EAP-EKE-Failure");
		eap_eke_state(data, FAILURE);
		return;
	}

	code = WPA_GET_BE32(payload);
	wpa_printf(MSG_DEBUG, "EAP-EKE: Peer reported failure code 0x%x", code);

	eap_eke_state(data, FAILURE);
}


static void eap_eke_process(struct eap_sm *sm, void *priv,
			     struct wpabuf *respData)
{
	struct eap_eke_data *data = priv;
	u8 eke_exch;
	size_t len;
	const u8 *pos, *end;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_EKE, respData, &len);
	if (pos == NULL || len < 1)
		return;

	eke_exch = *pos;
	end = pos + len;
	pos++;

	wpa_hexdump(MSG_DEBUG, "EAP-EKE: Received payload", pos, end - pos);

	switch (eke_exch) {
	case EAP_EKE_ID:
		eap_eke_process_identity(sm, data, respData, pos, end - pos);
		break;
	case EAP_EKE_COMMIT:
		eap_eke_process_commit(sm, data, respData, pos, end - pos);
		break;
	case EAP_EKE_CONFIRM:
		eap_eke_process_confirm(sm, data, respData, pos, end - pos);
		break;
	case EAP_EKE_FAILURE:
		eap_eke_process_failure(sm, data, respData, pos, end - pos);
		break;
	}
}


static Boolean eap_eke_isDone(struct eap_sm *sm, void *priv)
{
	struct eap_eke_data *data = priv;
	return data->state == SUCCESS || data->state == FAILURE;
}


static u8 * eap_eke_getKey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_eke_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_memdup(data->msk, EAP_MSK_LEN);
	if (key == NULL)
		return NULL;
	*len = EAP_MSK_LEN;

	return key;
}


static u8 * eap_eke_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_eke_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_memdup(data->emsk, EAP_EMSK_LEN);
	if (key == NULL)
		return NULL;
	*len = EAP_EMSK_LEN;

	return key;
}


static Boolean eap_eke_isSuccess(struct eap_sm *sm, void *priv)
{
	struct eap_eke_data *data = priv;
	return data->state == SUCCESS;
}


static u8 * eap_eke_get_session_id(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_eke_data *data = priv;
	u8 *sid;
	size_t sid_len;

	if (data->state != SUCCESS)
		return NULL;

	sid_len = 1 + 2 * data->sess.nonce_len;
	sid = os_malloc(sid_len);
	if (sid == NULL)
		return NULL;
	sid[0] = EAP_TYPE_EKE;
	os_memcpy(sid + 1, data->nonce_p, data->sess.nonce_len);
	os_memcpy(sid + 1 + data->sess.nonce_len, data->nonce_s,
		  data->sess.nonce_len);
	*len = sid_len;

	return sid;
}


int eap_server_eke_register(void)
{
	struct eap_method *eap;

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_IETF, EAP_TYPE_EKE, "EKE");
	if (eap == NULL)
		return -1;

	eap->init = eap_eke_init;
	eap->reset = eap_eke_reset;
	eap->buildReq = eap_eke_buildReq;
	eap->check = eap_eke_check;
	eap->process = eap_eke_process;
	eap->isDone = eap_eke_isDone;
	eap->getKey = eap_eke_getKey;
	eap->isSuccess = eap_eke_isSuccess;
	eap->get_emsk = eap_eke_get_emsk;
	eap->getSessionId = eap_eke_get_session_id;

	return eap_server_method_register(eap);
}
