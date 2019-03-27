/*
 * EAP peer method: EAP-EKE (RFC 6124)
 * Copyright (c) 2013, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/random.h"
#include "eap_peer/eap_i.h"
#include "eap_common/eap_eke_common.h"

struct eap_eke_data {
	enum {
		IDENTITY, COMMIT, CONFIRM, SUCCESS, FAILURE
	} state;
	u8 msk[EAP_MSK_LEN];
	u8 emsk[EAP_EMSK_LEN];
	u8 *peerid;
	size_t peerid_len;
	u8 *serverid;
	size_t serverid_len;
	u8 dh_priv[EAP_EKE_MAX_DH_LEN];
	struct eap_eke_session sess;
	u8 nonce_p[EAP_EKE_MAX_NONCE_LEN];
	u8 nonce_s[EAP_EKE_MAX_NONCE_LEN];
	struct wpabuf *msgs;
	u8 dhgroup; /* forced DH group or 0 to allow all supported */
	u8 encr; /* forced encryption algorithm or 0 to allow all supported */
	u8 prf; /* forced PRF or 0 to allow all supported */
	u8 mac; /* forced MAC or 0 to allow all supported */
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
		   eap_eke_state_txt(data->state), eap_eke_state_txt(state));
	data->state = state;
}


static void eap_eke_deinit(struct eap_sm *sm, void *priv);


static void * eap_eke_init(struct eap_sm *sm)
{
	struct eap_eke_data *data;
	const u8 *identity, *password;
	size_t identity_len, password_len;
	const char *phase1;

	password = eap_get_config_password(sm, &password_len);
	if (!password) {
		wpa_printf(MSG_INFO, "EAP-EKE: No password configured");
		return NULL;
	}

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	eap_eke_state(data, IDENTITY);

	identity = eap_get_config_identity(sm, &identity_len);
	if (identity) {
		data->peerid = os_memdup(identity, identity_len);
		if (data->peerid == NULL) {
			eap_eke_deinit(sm, data);
			return NULL;
		}
		data->peerid_len = identity_len;
	}

	phase1 = eap_get_config_phase1(sm);
	if (phase1) {
		const char *pos;

		pos = os_strstr(phase1, "dhgroup=");
		if (pos) {
			data->dhgroup = atoi(pos + 8);
			wpa_printf(MSG_DEBUG, "EAP-EKE: Forced dhgroup %u",
				   data->dhgroup);
		}

		pos = os_strstr(phase1, "encr=");
		if (pos) {
			data->encr = atoi(pos + 5);
			wpa_printf(MSG_DEBUG, "EAP-EKE: Forced encr %u",
				   data->encr);
		}

		pos = os_strstr(phase1, "prf=");
		if (pos) {
			data->prf = atoi(pos + 4);
			wpa_printf(MSG_DEBUG, "EAP-EKE: Forced prf %u",
				   data->prf);
		}

		pos = os_strstr(phase1, "mac=");
		if (pos) {
			data->mac = atoi(pos + 4);
			wpa_printf(MSG_DEBUG, "EAP-EKE: Forced mac %u",
				   data->mac);
		}
	}

	return data;
}


static void eap_eke_deinit(struct eap_sm *sm, void *priv)
{
	struct eap_eke_data *data = priv;
	eap_eke_session_clean(&data->sess);
	os_free(data->serverid);
	os_free(data->peerid);
	wpabuf_free(data->msgs);
	bin_clear_free(data, sizeof(*data));
}


static struct wpabuf * eap_eke_build_msg(struct eap_eke_data *data, int id,
					 size_t length, u8 eke_exch)
{
	struct wpabuf *msg;
	size_t plen;

	plen = 1 + length;

	msg = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_EKE, plen,
			    EAP_CODE_RESPONSE, id);
	if (msg == NULL) {
		wpa_printf(MSG_ERROR, "EAP-EKE: Failed to allocate memory");
		return NULL;
	}

	wpabuf_put_u8(msg, eke_exch);

	return msg;
}


static int eap_eke_supp_dhgroup(u8 dhgroup)
{
	return dhgroup == EAP_EKE_DHGROUP_EKE_2 ||
		dhgroup == EAP_EKE_DHGROUP_EKE_5 ||
		dhgroup == EAP_EKE_DHGROUP_EKE_14 ||
		dhgroup == EAP_EKE_DHGROUP_EKE_15 ||
		dhgroup == EAP_EKE_DHGROUP_EKE_16;
}


static int eap_eke_supp_encr(u8 encr)
{
	return encr == EAP_EKE_ENCR_AES128_CBC;
}


static int eap_eke_supp_prf(u8 prf)
{
	return prf == EAP_EKE_PRF_HMAC_SHA1 ||
		prf == EAP_EKE_PRF_HMAC_SHA2_256;
}


static int eap_eke_supp_mac(u8 mac)
{
	return mac == EAP_EKE_MAC_HMAC_SHA1 ||
		mac == EAP_EKE_MAC_HMAC_SHA2_256;
}


static struct wpabuf * eap_eke_build_fail(struct eap_eke_data *data,
					  struct eap_method_ret *ret,
					  u8 id, u32 failure_code)
{
	struct wpabuf *resp;

	wpa_printf(MSG_DEBUG, "EAP-EKE: Sending EAP-EKE-Failure/Response - code=0x%x",
		   failure_code);

	resp = eap_eke_build_msg(data, id, 4, EAP_EKE_FAILURE);
	if (resp)
		wpabuf_put_be32(resp, failure_code);

	os_memset(data->dh_priv, 0, sizeof(data->dh_priv));
	eap_eke_session_clean(&data->sess);

	eap_eke_state(data, FAILURE);
	ret->methodState = METHOD_DONE;
	ret->decision = DECISION_FAIL;
	ret->allowNotifications = FALSE;

	return resp;
}


static struct wpabuf * eap_eke_process_id(struct eap_eke_data *data,
					  struct eap_method_ret *ret,
					  const struct wpabuf *reqData,
					  const u8 *payload,
					  size_t payload_len)
{
	struct wpabuf *resp;
	unsigned num_prop, i;
	const u8 *pos, *end;
	const u8 *prop = NULL;
	u8 idtype;
	u8 id = eap_get_id(reqData);

	if (data->state != IDENTITY) {
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PROTO_ERROR);
	}

	wpa_printf(MSG_DEBUG, "EAP-EKE: Received EAP-EKE-ID/Request");

	if (payload_len < 2 + 4) {
		wpa_printf(MSG_DEBUG, "EAP-EKE: Too short ID/Request Data");
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PROTO_ERROR);
	}

	pos = payload;
	end = payload + payload_len;

	num_prop = *pos++;
	pos++; /* Ignore Reserved field */

	if (pos + num_prop * 4 > end) {
		wpa_printf(MSG_DEBUG, "EAP-EKE: Too short ID/Request Data (num_prop=%u)",
			   num_prop);
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PROTO_ERROR);
	}

	for (i = 0; i < num_prop; i++) {
		const u8 *tmp = pos;

		wpa_printf(MSG_DEBUG, "EAP-EKE: Proposal #%u: dh=%u encr=%u prf=%u mac=%u",
			   i, pos[0], pos[1], pos[2], pos[3]);
		pos += 4;

		if ((data->dhgroup && data->dhgroup != *tmp) ||
		    !eap_eke_supp_dhgroup(*tmp))
			continue;
		tmp++;
		if ((data->encr && data->encr != *tmp) ||
		    !eap_eke_supp_encr(*tmp))
			continue;
		tmp++;
		if ((data->prf && data->prf != *tmp) ||
		    !eap_eke_supp_prf(*tmp))
			continue;
		tmp++;
		if ((data->mac && data->mac != *tmp) ||
		    !eap_eke_supp_mac(*tmp))
			continue;

		prop = tmp - 3;
		if (eap_eke_session_init(&data->sess, prop[0], prop[1], prop[2],
					 prop[3]) < 0) {
			prop = NULL;
			continue;
		}

		wpa_printf(MSG_DEBUG, "EAP-EKE: Selected proposal");
		break;
	}

	if (prop == NULL) {
		wpa_printf(MSG_DEBUG, "EAP-EKE: No acceptable proposal found");
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_NO_PROPOSAL_CHOSEN);
	}

	pos += (num_prop - i - 1) * 4;

	if (pos == end) {
		wpa_printf(MSG_DEBUG, "EAP-EKE: Too short ID/Request Data to include IDType/Identity");
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PROTO_ERROR);
	}

	idtype = *pos++;
	wpa_printf(MSG_DEBUG, "EAP-EKE: Server IDType %u", idtype);
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-EKE: Server Identity",
			  pos, end - pos);
	os_free(data->serverid);
	data->serverid = os_memdup(pos, end - pos);
	if (data->serverid == NULL) {
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}
	data->serverid_len = end - pos;

	wpa_printf(MSG_DEBUG, "EAP-EKE: Sending EAP-EKE-ID/Response");

	resp = eap_eke_build_msg(data, id,
				 2 + 4 + 1 + data->peerid_len,
				 EAP_EKE_ID);
	if (resp == NULL) {
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}

	wpabuf_put_u8(resp, 1); /* NumProposals */
	wpabuf_put_u8(resp, 0); /* Reserved */
	wpabuf_put_data(resp, prop, 4); /* Selected Proposal */
	wpabuf_put_u8(resp, EAP_EKE_ID_NAI);
	if (data->peerid)
		wpabuf_put_data(resp, data->peerid, data->peerid_len);

	wpabuf_free(data->msgs);
	data->msgs = wpabuf_alloc(wpabuf_len(reqData) + wpabuf_len(resp));
	if (data->msgs == NULL) {
		wpabuf_free(resp);
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}
	wpabuf_put_buf(data->msgs, reqData);
	wpabuf_put_buf(data->msgs, resp);

	eap_eke_state(data, COMMIT);

	return resp;
}


static struct wpabuf * eap_eke_process_commit(struct eap_sm *sm,
					      struct eap_eke_data *data,
					      struct eap_method_ret *ret,
					      const struct wpabuf *reqData,
					      const u8 *payload,
					      size_t payload_len)
{
	struct wpabuf *resp;
	const u8 *pos, *end, *dhcomp;
	size_t prot_len;
	u8 *rpos;
	u8 key[EAP_EKE_MAX_KEY_LEN];
	u8 pub[EAP_EKE_MAX_DH_LEN];
	const u8 *password;
	size_t password_len;
	u8 id = eap_get_id(reqData);

	if (data->state != COMMIT) {
		wpa_printf(MSG_DEBUG, "EAP-EKE: EAP-EKE-Commit/Request received in unexpected state (%d)", data->state);
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PROTO_ERROR);
	}

	wpa_printf(MSG_DEBUG, "EAP-EKE: Received EAP-EKE-Commit/Request");

	password = eap_get_config_password(sm, &password_len);
	if (password == NULL) {
		wpa_printf(MSG_INFO, "EAP-EKE: No password configured!");
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PASSWD_NOT_FOUND);
	}

	pos = payload;
	end = payload + payload_len;

	if (pos + data->sess.dhcomp_len > end) {
		wpa_printf(MSG_DEBUG, "EAP-EKE: Too short EAP-EKE-Commit");
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PROTO_ERROR);
	}

	wpa_hexdump(MSG_DEBUG, "EAP-EKE: DHComponent_S",
		    pos, data->sess.dhcomp_len);
	dhcomp = pos;
	pos += data->sess.dhcomp_len;
	wpa_hexdump(MSG_DEBUG, "EAP-EKE: CBValue", pos, end - pos);

	/*
	 * temp = prf(0+, password)
	 * key = prf+(temp, ID_S | ID_P)
	 */
	if (eap_eke_derive_key(&data->sess, password, password_len,
			       data->serverid, data->serverid_len,
			       data->peerid, data->peerid_len, key) < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to derive key");
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}

	/*
	 * y_p = g ^ x_p (mod p)
	 * x_p = random number 2 .. p-1
	 */
	if (eap_eke_dh_init(data->sess.dhgroup, data->dh_priv, pub) < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to initialize DH");
		os_memset(key, 0, sizeof(key));
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}

	if (eap_eke_shared_secret(&data->sess, key, data->dh_priv, dhcomp) < 0)
	{
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to derive shared secret");
		os_memset(key, 0, sizeof(key));
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}

	if (eap_eke_derive_ke_ki(&data->sess,
				 data->serverid, data->serverid_len,
				 data->peerid, data->peerid_len) < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to derive Ke/Ki");
		os_memset(key, 0, sizeof(key));
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}

	wpa_printf(MSG_DEBUG, "EAP-EKE: Sending EAP-EKE-Commit/Response");

	resp = eap_eke_build_msg(data, id,
				 data->sess.dhcomp_len + data->sess.pnonce_len,
				 EAP_EKE_COMMIT);
	if (resp == NULL) {
		os_memset(key, 0, sizeof(key));
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}

	/* DHComponent_P = Encr(key, y_p) */
	rpos = wpabuf_put(resp, data->sess.dhcomp_len);
	if (eap_eke_dhcomp(&data->sess, key, pub, rpos) < 0) {
		wpabuf_free(resp);
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to build DHComponent_P");
		os_memset(key, 0, sizeof(key));
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}
	os_memset(key, 0, sizeof(key));

	wpa_hexdump(MSG_DEBUG, "EAP-EKE: DHComponent_P",
		    rpos, data->sess.dhcomp_len);

	if (random_get_bytes(data->nonce_p, data->sess.nonce_len)) {
		wpabuf_free(resp);
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: Nonce_P",
			data->nonce_p, data->sess.nonce_len);
	prot_len = wpabuf_tailroom(resp);
	if (eap_eke_prot(&data->sess, data->nonce_p, data->sess.nonce_len,
			 wpabuf_put(resp, 0), &prot_len) < 0) {
		wpabuf_free(resp);
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}
	wpa_hexdump(MSG_DEBUG, "EAP-EKE: PNonce_P",
		    wpabuf_put(resp, 0), prot_len);
	wpabuf_put(resp, prot_len);

	/* TODO: CBValue */

	if (wpabuf_resize(&data->msgs, wpabuf_len(reqData) + wpabuf_len(resp))
	    < 0) {
		wpabuf_free(resp);
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}
	wpabuf_put_buf(data->msgs, reqData);
	wpabuf_put_buf(data->msgs, resp);

	eap_eke_state(data, CONFIRM);

	return resp;
}


static struct wpabuf * eap_eke_process_confirm(struct eap_eke_data *data,
					       struct eap_method_ret *ret,
					       const struct wpabuf *reqData,
					       const u8 *payload,
					       size_t payload_len)
{
	struct wpabuf *resp;
	const u8 *pos, *end;
	size_t prot_len;
	u8 nonces[2 * EAP_EKE_MAX_NONCE_LEN];
	u8 auth_s[EAP_EKE_MAX_HASH_LEN];
	size_t decrypt_len;
	u8 *auth;
	u8 id = eap_get_id(reqData);

	if (data->state != CONFIRM) {
		wpa_printf(MSG_DEBUG, "EAP-EKE: EAP-EKE-Confirm/Request received in unexpected state (%d)",
			   data->state);
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PROTO_ERROR);
	}

	wpa_printf(MSG_DEBUG, "EAP-EKE: Received EAP-EKE-Confirm/Request");

	pos = payload;
	end = payload + payload_len;

	if (pos + data->sess.pnonce_ps_len + data->sess.prf_len > end) {
		wpa_printf(MSG_DEBUG, "EAP-EKE: Too short EAP-EKE-Confirm");
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PROTO_ERROR);
	}

	decrypt_len = sizeof(nonces);
	if (eap_eke_decrypt_prot(&data->sess, pos, data->sess.pnonce_ps_len,
				 nonces, &decrypt_len) < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to decrypt PNonce_PS");
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_AUTHENTICATION_FAIL);
	}
	if (decrypt_len != (size_t) 2 * data->sess.nonce_len) {
		wpa_printf(MSG_INFO, "EAP-EKE: PNonce_PS protected data length does not match length of Nonce_P and Nonce_S");
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_AUTHENTICATION_FAIL);
	}
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: Received Nonce_P | Nonce_S",
			nonces, 2 * data->sess.nonce_len);
	if (os_memcmp(data->nonce_p, nonces, data->sess.nonce_len) != 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Received Nonce_P does not match transmitted Nonce_P");
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_AUTHENTICATION_FAIL);
	}

	os_memcpy(data->nonce_s, nonces + data->sess.nonce_len,
		  data->sess.nonce_len);
	wpa_hexdump_key(MSG_DEBUG, "EAP-EKE: Nonce_S",
			data->nonce_s, data->sess.nonce_len);

	if (eap_eke_derive_ka(&data->sess, data->serverid, data->serverid_len,
			      data->peerid, data->peerid_len,
			      data->nonce_p, data->nonce_s) < 0) {
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}

	if (eap_eke_auth(&data->sess, "EAP-EKE server", data->msgs, auth_s) < 0)
	{
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}
	wpa_hexdump(MSG_DEBUG, "EAP-EKE: Auth_S", auth_s, data->sess.prf_len);
	if (os_memcmp_const(auth_s, pos + data->sess.pnonce_ps_len,
			    data->sess.prf_len) != 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Auth_S does not match");
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_AUTHENTICATION_FAIL);
	}

	wpa_printf(MSG_DEBUG, "EAP-EKE: Sending EAP-EKE-Confirm/Response");

	resp = eap_eke_build_msg(data, id,
				 data->sess.pnonce_len + data->sess.prf_len,
				 EAP_EKE_CONFIRM);
	if (resp == NULL) {
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}

	prot_len = wpabuf_tailroom(resp);
	if (eap_eke_prot(&data->sess, data->nonce_s, data->sess.nonce_len,
			 wpabuf_put(resp, 0), &prot_len) < 0) {
		wpabuf_free(resp);
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}
	wpabuf_put(resp, prot_len);

	auth = wpabuf_put(resp, data->sess.prf_len);
	if (eap_eke_auth(&data->sess, "EAP-EKE peer", data->msgs, auth) < 0) {
		wpabuf_free(resp);
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}
	wpa_hexdump(MSG_DEBUG, "EAP-EKE: Auth_P", auth, data->sess.prf_len);

	if (eap_eke_derive_msk(&data->sess, data->serverid, data->serverid_len,
			       data->peerid, data->peerid_len,
			       data->nonce_s, data->nonce_p,
			       data->msk, data->emsk) < 0) {
		wpa_printf(MSG_INFO, "EAP-EKE: Failed to derive MSK/EMSK");
		wpabuf_free(resp);
		return eap_eke_build_fail(data, ret, id,
					  EAP_EKE_FAIL_PRIVATE_INTERNAL_ERROR);
	}

	os_memset(data->dh_priv, 0, sizeof(data->dh_priv));
	eap_eke_session_clean(&data->sess);

	eap_eke_state(data, SUCCESS);
	ret->methodState = METHOD_MAY_CONT;
	ret->decision = DECISION_COND_SUCC;
	ret->allowNotifications = FALSE;

	return resp;
}


static struct wpabuf * eap_eke_process_failure(struct eap_eke_data *data,
					       struct eap_method_ret *ret,
					       const struct wpabuf *reqData,
					       const u8 *payload,
					       size_t payload_len)
{
	wpa_printf(MSG_DEBUG, "EAP-EKE: Received EAP-EKE-Failure/Request");

	if (payload_len < 4) {
		wpa_printf(MSG_DEBUG, "EAP-EKE: Too short EAP-EKE-Failure");
	} else {
		u32 code;
		code = WPA_GET_BE32(payload);
		wpa_printf(MSG_INFO, "EAP-EKE: Failure-Code 0x%x", code);
	}

	return eap_eke_build_fail(data, ret, eap_get_id(reqData),
				  EAP_EKE_FAIL_NO_ERROR);
}


static struct wpabuf * eap_eke_process(struct eap_sm *sm, void *priv,
				       struct eap_method_ret *ret,
				       const struct wpabuf *reqData)
{
	struct eap_eke_data *data = priv;
	struct wpabuf *resp;
	const u8 *pos, *end;
	size_t len;
	u8 eke_exch;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_EKE, reqData, &len);
	if (pos == NULL || len < 1) {
		ret->ignore = TRUE;
		return NULL;
	}

	end = pos + len;
	eke_exch = *pos++;

	wpa_printf(MSG_DEBUG, "EAP-EKE: Received frame: exch %d", eke_exch);
	wpa_hexdump(MSG_DEBUG, "EAP-EKE: Received Data", pos, end - pos);

	ret->ignore = FALSE;
	ret->methodState = METHOD_MAY_CONT;
	ret->decision = DECISION_FAIL;
	ret->allowNotifications = TRUE;

	switch (eke_exch) {
	case EAP_EKE_ID:
		resp = eap_eke_process_id(data, ret, reqData, pos, end - pos);
		break;
	case EAP_EKE_COMMIT:
		resp = eap_eke_process_commit(sm, data, ret, reqData,
					      pos, end - pos);
		break;
	case EAP_EKE_CONFIRM:
		resp = eap_eke_process_confirm(data, ret, reqData,
					       pos, end - pos);
		break;
	case EAP_EKE_FAILURE:
		resp = eap_eke_process_failure(data, ret, reqData,
					       pos, end - pos);
		break;
	default:
		wpa_printf(MSG_DEBUG, "EAP-EKE: Ignoring message with unknown EKE-Exch %d", eke_exch);
		ret->ignore = TRUE;
		return NULL;
	}

	if (ret->methodState == METHOD_DONE)
		ret->allowNotifications = FALSE;

	return resp;
}


static Boolean eap_eke_isKeyAvailable(struct eap_sm *sm, void *priv)
{
	struct eap_eke_data *data = priv;
	return data->state == SUCCESS;
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


int eap_peer_eke_register(void)
{
	struct eap_method *eap;

	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_EKE, "EKE");
	if (eap == NULL)
		return -1;

	eap->init = eap_eke_init;
	eap->deinit = eap_eke_deinit;
	eap->process = eap_eke_process;
	eap->isKeyAvailable = eap_eke_isKeyAvailable;
	eap->getKey = eap_eke_getKey;
	eap->get_emsk = eap_eke_get_emsk;
	eap->getSessionId = eap_eke_get_session_id;

	return eap_peer_method_register(eap);
}
