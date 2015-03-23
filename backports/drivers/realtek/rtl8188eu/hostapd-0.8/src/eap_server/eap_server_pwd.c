/*
 * hostapd / EAP-pwd (RFC 5931) server
 * Copyright (c) 2010, Dan Harkins <dharkins@lounge.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the BSD license.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"
#include "eap_server/eap_i.h"
#include "eap_common/eap_pwd_common.h"


struct eap_pwd_data {
	enum {
		PWD_ID_Req, PWD_Commit_Req, PWD_Confirm_Req, SUCCESS, FAILURE
	} state;
	u8 *id_peer;
	size_t id_peer_len;
	u8 *id_server;
	size_t id_server_len;
	u8 *password;
	size_t password_len;
	u32 token;
	u16 group_num;
	EAP_PWD_group *grp;

	BIGNUM *k;
	BIGNUM *private_value;
	BIGNUM *peer_scalar;
	BIGNUM *my_scalar;
	EC_POINT *my_element;
	EC_POINT *peer_element;

	u8 my_confirm[SHA256_DIGEST_LENGTH];

	u8 msk[EAP_MSK_LEN];
	u8 emsk[EAP_EMSK_LEN];

	BN_CTX *bnctx;
};


static const char * eap_pwd_state_txt(int state)
{
	switch (state) {
        case PWD_ID_Req:
		return "PWD-ID-Req";
        case PWD_Commit_Req:
		return "PWD-Commit-Req";
        case PWD_Confirm_Req:
		return "PWD-Confirm-Req";
        case SUCCESS:
		return "SUCCESS";
        case FAILURE:
		return "FAILURE";
        default:
		return "PWD-Unk";
	}
}


static void eap_pwd_state(struct eap_pwd_data *data, int state)
{
	wpa_printf(MSG_DEBUG, "EAP-pwd: %s -> %s",
		   eap_pwd_state_txt(data->state), eap_pwd_state_txt(state));
	data->state = state;
}


static void * eap_pwd_init(struct eap_sm *sm)
{
	struct eap_pwd_data *data;

	if (sm->user == NULL || sm->user->password == NULL ||
	    sm->user->password_len == 0) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): Password is not "
			   "configured");
		return NULL;
	}

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;

	data->group_num = sm->pwd_group;
	wpa_printf(MSG_DEBUG, "EAP-pwd: Selected group number %d",
		   data->group_num);
	data->state = PWD_ID_Req;

	data->id_server = (u8 *) os_strdup("server");
	if (data->id_server)
		data->id_server_len = os_strlen((char *) data->id_server);

	data->password = os_malloc(sm->user->password_len);
	if (data->password == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: Memory allocation password "
			   "fail");
		os_free(data->id_server);
		os_free(data);
		return NULL;
	}
	data->password_len = sm->user->password_len;
	os_memcpy(data->password, sm->user->password, data->password_len);

	data->bnctx = BN_CTX_new();
	if (data->bnctx == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: bn context allocation fail");
		os_free(data->password);
		os_free(data->id_server);
		os_free(data);
		return NULL;
	}

	return data;
}


static void eap_pwd_reset(struct eap_sm *sm, void *priv)
{
	struct eap_pwd_data *data = priv;

	BN_free(data->private_value);
	BN_free(data->peer_scalar);
	BN_free(data->my_scalar);
	BN_free(data->k);
	BN_CTX_free(data->bnctx);
	EC_POINT_free(data->my_element);
	EC_POINT_free(data->peer_element);
	os_free(data->id_peer);
	os_free(data->id_server);
	os_free(data->password);
	if (data->grp) {
		EC_GROUP_free(data->grp->group);
		EC_POINT_free(data->grp->pwe);
		BN_free(data->grp->order);
		BN_free(data->grp->prime);
		os_free(data->grp);
	}
	os_free(data);
}


static struct wpabuf *
eap_pwd_build_id_req(struct eap_sm *sm, struct eap_pwd_data *data, u8 id)
{
	struct wpabuf *req;

	wpa_printf(MSG_DEBUG, "EAP-pwd: ID/Request");
	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PWD,
			    sizeof(struct eap_pwd_hdr) +
			    sizeof(struct eap_pwd_id) + data->id_server_len,
			    EAP_CODE_REQUEST, id);
	if (req == NULL) {
		eap_pwd_state(data, FAILURE);
		return NULL;
	}

	/* an lfsr is good enough to generate unpredictable tokens */
	data->token = os_random();
	wpabuf_put_u8(req, EAP_PWD_OPCODE_ID_EXCH);
	wpabuf_put_be16(req, data->group_num);
	wpabuf_put_u8(req, EAP_PWD_DEFAULT_RAND_FUNC);
	wpabuf_put_u8(req, EAP_PWD_DEFAULT_PRF);
	wpabuf_put_data(req, &data->token, sizeof(data->token));
	wpabuf_put_u8(req, EAP_PWD_PREP_NONE);
	wpabuf_put_data(req, data->id_server, data->id_server_len);

	return req;
}


static struct wpabuf *
eap_pwd_build_commit_req(struct eap_sm *sm, struct eap_pwd_data *data, u8 id)
{
	struct wpabuf *req = NULL;
	BIGNUM *mask = NULL, *x = NULL, *y = NULL;
	u8 *scalar = NULL, *element = NULL;
	u16 offset;

	wpa_printf(MSG_DEBUG, "EAP-pwd: Commit/Request");

	if (((data->private_value = BN_new()) == NULL) ||
	    ((data->my_element = EC_POINT_new(data->grp->group)) == NULL) ||
	    ((data->my_scalar = BN_new()) == NULL) ||
	    ((mask = BN_new()) == NULL)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): scalar allocation "
			   "fail");
		goto fin;
	}

	BN_rand_range(data->private_value, data->grp->order);
	BN_rand_range(mask, data->grp->order);
	BN_add(data->my_scalar, data->private_value, mask);
	BN_mod(data->my_scalar, data->my_scalar, data->grp->order,
	       data->bnctx);

	if (!EC_POINT_mul(data->grp->group, data->my_element, NULL,
			  data->grp->pwe, mask, data->bnctx)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): element allocation "
			   "fail");
		eap_pwd_state(data, FAILURE);
		goto fin;
	}

	if (!EC_POINT_invert(data->grp->group, data->my_element, data->bnctx))
	{
		wpa_printf(MSG_INFO, "EAP-PWD (server): element inversion "
			   "fail");
		goto fin;
	}
	BN_free(mask);

	if (((x = BN_new()) == NULL) ||
	    ((y = BN_new()) == NULL)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): point allocation "
			   "fail");
		goto fin;
	}
	if (!EC_POINT_get_affine_coordinates_GFp(data->grp->group,
						 data->my_element, x, y,
						 data->bnctx)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): point assignment "
			   "fail");
		goto fin;
	}

	if (((scalar = os_malloc(BN_num_bytes(data->grp->order))) == NULL) ||
	    ((element = os_malloc(BN_num_bytes(data->grp->prime) * 2)) ==
	     NULL)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): data allocation fail");
		goto fin;
	}

	/*
	 * bignums occupy as little memory as possible so one that is
	 * sufficiently smaller than the prime or order might need pre-pending
	 * with zeros.
	 */
	os_memset(scalar, 0, BN_num_bytes(data->grp->order));
	os_memset(element, 0, BN_num_bytes(data->grp->prime) * 2);
	offset = BN_num_bytes(data->grp->order) -
		BN_num_bytes(data->my_scalar);
	BN_bn2bin(data->my_scalar, scalar + offset);

	offset = BN_num_bytes(data->grp->prime) - BN_num_bytes(x);
	BN_bn2bin(x, element + offset);
	offset = BN_num_bytes(data->grp->prime) - BN_num_bytes(y);
	BN_bn2bin(y, element + BN_num_bytes(data->grp->prime) + offset);

	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PWD,
			    sizeof(struct eap_pwd_hdr) +
			    (2 * BN_num_bytes(data->grp->prime)) +
			    BN_num_bytes(data->grp->order),
			    EAP_CODE_REQUEST, id);
	if (req == NULL)
		goto fin;
	wpabuf_put_u8(req, EAP_PWD_OPCODE_COMMIT_EXCH);

	/* We send the element as (x,y) followed by the scalar */
	wpabuf_put_data(req, element, (2 * BN_num_bytes(data->grp->prime)));
	wpabuf_put_data(req, scalar, BN_num_bytes(data->grp->order));

fin:
	os_free(scalar);
	os_free(element);
	BN_free(x);
	BN_free(y);
	if (req == NULL)
		eap_pwd_state(data, FAILURE);

	return req;
}


static struct wpabuf *
eap_pwd_build_confirm_req(struct eap_sm *sm, struct eap_pwd_data *data, u8 id)
{
	struct wpabuf *req = NULL;
	BIGNUM *x = NULL, *y = NULL;
	HMAC_CTX ctx;
	u8 conf[SHA256_DIGEST_LENGTH], *cruft = NULL, *ptr;
	u16 grp;

	wpa_printf(MSG_DEBUG, "EAP-pwd: Confirm/Request");

	/* Each component of the cruft will be at most as big as the prime */
	if (((cruft = os_malloc(BN_num_bytes(data->grp->prime))) == NULL) ||
	    ((x = BN_new()) == NULL) || ((y = BN_new()) == NULL)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): debug allocation "
			   "fail");
		goto fin;
	}

	/*
	 * commit is H(k | server_element | server_scalar | peer_element |
	 *	       peer_scalar | ciphersuite)
	 */
	H_Init(&ctx);

	/*
	 * Zero the memory each time because this is mod prime math and some
	 * value may start with a few zeros and the previous one did not.
	 *
	 * First is k
	 */
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(data->k, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->prime));

	/* server element: x, y */
	if (!EC_POINT_get_affine_coordinates_GFp(data->grp->group,
						 data->my_element, x, y,
						 data->bnctx)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): confirm point "
			   "assignment fail");
		goto fin;
	}

	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(x, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->prime));
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(y, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->prime));

	/* server scalar */
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(data->my_scalar, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->order));

	/* peer element: x, y */
	if (!EC_POINT_get_affine_coordinates_GFp(data->grp->group,
						 data->peer_element, x, y,
						 data->bnctx)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): confirm point "
			   "assignment fail");
		goto fin;
	}

	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(x, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->prime));
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(y, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->prime));

	/* peer scalar */
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(data->peer_scalar, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->order));

	/* ciphersuite */
	grp = htons(data->group_num);
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	ptr = cruft;
	os_memcpy(ptr, &grp, sizeof(u16));
	ptr += sizeof(u16);
	*ptr = EAP_PWD_DEFAULT_RAND_FUNC;
	ptr += sizeof(u8);
	*ptr = EAP_PWD_DEFAULT_PRF;
	ptr += sizeof(u8);
	H_Update(&ctx, cruft, ptr-cruft);

	/* all done with the random function */
	H_Final(&ctx, conf);
	os_memcpy(data->my_confirm, conf, SHA256_DIGEST_LENGTH);

	req = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PWD,
			    sizeof(struct eap_pwd_hdr) + SHA256_DIGEST_LENGTH,
			    EAP_CODE_REQUEST, id);
	if (req == NULL)
		goto fin;

	wpabuf_put_u8(req, EAP_PWD_OPCODE_CONFIRM_EXCH);
	wpabuf_put_data(req, conf, SHA256_DIGEST_LENGTH);

fin:
	os_free(cruft);
	BN_free(x);
	BN_free(y);
	if (req == NULL)
		eap_pwd_state(data, FAILURE);

	return req;
}


static struct wpabuf *
eap_pwd_build_req(struct eap_sm *sm, void *priv, u8 id)
{
	struct eap_pwd_data *data = priv;

	switch (data->state) {
        case PWD_ID_Req:
		return eap_pwd_build_id_req(sm, data, id);
        case PWD_Commit_Req:
		return eap_pwd_build_commit_req(sm, data, id);
        case PWD_Confirm_Req:
		return eap_pwd_build_confirm_req(sm, data, id);
        default:
		wpa_printf(MSG_INFO, "EAP-pwd: Unknown state %d in build_req",
			   data->state);
		break;
	}

	return NULL;
}


static Boolean eap_pwd_check(struct eap_sm *sm, void *priv,
			     struct wpabuf *respData)
{
	struct eap_pwd_data *data = priv;
	const u8 *pos;
	size_t len;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_PWD, respData, &len);
	if (pos == NULL || len < 1) {
		wpa_printf(MSG_INFO, "EAP-pwd: Invalid frame");
		return TRUE;
	}

	wpa_printf(MSG_DEBUG, "EAP-pwd: Received frame: opcode=%d", *pos);

	if (data->state == PWD_ID_Req && *pos == EAP_PWD_OPCODE_ID_EXCH)
		return FALSE;

	if (data->state == PWD_Commit_Req &&
	    *pos == EAP_PWD_OPCODE_COMMIT_EXCH)
		return FALSE;

	if (data->state == PWD_Confirm_Req &&
	    *pos == EAP_PWD_OPCODE_CONFIRM_EXCH)
		return FALSE;

	wpa_printf(MSG_INFO, "EAP-pwd: Unexpected opcode=%d in state=%d",
		   *pos, data->state);

	return TRUE;
}


static void eap_pwd_process_id_resp(struct eap_sm *sm,
				    struct eap_pwd_data *data,
				    const u8 *payload, size_t payload_len)
{
	struct eap_pwd_id *id;

	if (payload_len < sizeof(struct eap_pwd_id)) {
		wpa_printf(MSG_INFO, "EAP-pwd: Invalid ID response");
		return;
	}

	id = (struct eap_pwd_id *) payload;
	if ((data->group_num != be_to_host16(id->group_num)) ||
	    (id->random_function != EAP_PWD_DEFAULT_RAND_FUNC) ||
	    (os_memcmp(id->token, (u8 *)&data->token, sizeof(data->token))) ||
	    (id->prf != EAP_PWD_DEFAULT_PRF)) {
		wpa_printf(MSG_INFO, "EAP-pwd: peer changed parameters");
		eap_pwd_state(data, FAILURE);
		return;
	}
	data->id_peer = os_malloc(payload_len - sizeof(struct eap_pwd_id));
	if (data->id_peer == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: memory allocation id fail");
		return;
	}
	data->id_peer_len = payload_len - sizeof(struct eap_pwd_id);
	os_memcpy(data->id_peer, id->identity, data->id_peer_len);
	wpa_hexdump_ascii(MSG_DEBUG, "EAP-PWD (server): peer sent id of",
			  data->id_peer, data->id_peer_len);

	if ((data->grp = os_malloc(sizeof(EAP_PWD_group))) == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: failed to allocate memory for "
			   "group");
		return;
	}
	if (compute_password_element(data->grp, data->group_num,
				     data->password, data->password_len,
				     data->id_server, data->id_server_len,
				     data->id_peer, data->id_peer_len,
				     (u8 *) &data->token)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): unable to compute "
			   "PWE");
		return;
	}
	wpa_printf(MSG_DEBUG, "EAP-PWD (server): computed %d bit PWE...",
		   BN_num_bits(data->grp->prime));

	eap_pwd_state(data, PWD_Commit_Req);
}


static void
eap_pwd_process_commit_resp(struct eap_sm *sm, struct eap_pwd_data *data,
			    const u8 *payload, size_t payload_len)
{
	u8 *ptr;
	BIGNUM *x = NULL, *y = NULL, *cofactor = NULL;
	EC_POINT *K = NULL, *point = NULL;
	int res = 0;

	wpa_printf(MSG_DEBUG, "EAP-pwd: Received commit response");

	if (((data->peer_scalar = BN_new()) == NULL) ||
	    ((data->k = BN_new()) == NULL) ||
	    ((cofactor = BN_new()) == NULL) ||
	    ((x = BN_new()) == NULL) ||
	    ((y = BN_new()) == NULL) ||
	    ((point = EC_POINT_new(data->grp->group)) == NULL) ||
	    ((K = EC_POINT_new(data->grp->group)) == NULL) ||
	    ((data->peer_element = EC_POINT_new(data->grp->group)) == NULL)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): peer data allocation "
			   "fail");
		goto fin;
	}

	if (!EC_GROUP_get_cofactor(data->grp->group, cofactor, NULL)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): unable to get "
			   "cofactor for curve");
		goto fin;
	}

	/* element, x then y, followed by scalar */
	ptr = (u8 *) payload;
	BN_bin2bn(ptr, BN_num_bytes(data->grp->prime), x);
	ptr += BN_num_bytes(data->grp->prime);
	BN_bin2bn(ptr, BN_num_bytes(data->grp->prime), y);
	ptr += BN_num_bytes(data->grp->prime);
	BN_bin2bn(ptr, BN_num_bytes(data->grp->order), data->peer_scalar);
	if (!EC_POINT_set_affine_coordinates_GFp(data->grp->group,
						 data->peer_element, x, y,
						 data->bnctx)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): setting peer element "
			   "fail");
		goto fin;
	}

	/* check to ensure peer's element is not in a small sub-group */
	if (BN_cmp(cofactor, BN_value_one())) {
		if (!EC_POINT_mul(data->grp->group, point, NULL,
				  data->peer_element, cofactor, NULL)) {
			wpa_printf(MSG_INFO, "EAP-PWD (server): cannot "
				   "multiply peer element by order");
			goto fin;
		}
		if (EC_POINT_is_at_infinity(data->grp->group, point)) {
			wpa_printf(MSG_INFO, "EAP-PWD (server): peer element "
				   "is at infinity!\n");
			goto fin;
		}
	}

	/* compute the shared key, k */
	if ((!EC_POINT_mul(data->grp->group, K, NULL, data->grp->pwe,
			   data->peer_scalar, data->bnctx)) ||
	    (!EC_POINT_add(data->grp->group, K, K, data->peer_element,
			   data->bnctx)) ||
	    (!EC_POINT_mul(data->grp->group, K, NULL, K, data->private_value,
			   data->bnctx))) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): computing shared key "
			   "fail");
		goto fin;
	}

	/* ensure that the shared key isn't in a small sub-group */
	if (BN_cmp(cofactor, BN_value_one())) {
		if (!EC_POINT_mul(data->grp->group, K, NULL, K, cofactor,
				  NULL)) {
			wpa_printf(MSG_INFO, "EAP-PWD (server): cannot "
				   "multiply shared key point by order!\n");
			goto fin;
		}
	}

	/*
	 * This check is strictly speaking just for the case above where
	 * co-factor > 1 but it was suggested that even though this is probably
	 * never going to happen it is a simple and safe check "just to be
	 * sure" so let's be safe.
	 */
	if (EC_POINT_is_at_infinity(data->grp->group, K)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): shared key point is "
			   "at infinity");
		goto fin;
	}
	if (!EC_POINT_get_affine_coordinates_GFp(data->grp->group, K, data->k,
						 NULL, data->bnctx)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): unable to extract "
			   "shared secret from secret point");
		goto fin;
	}
	res = 1;

fin:
	EC_POINT_free(K);
	EC_POINT_free(point);
	BN_free(cofactor);
	BN_free(x);
	BN_free(y);

	if (res)
		eap_pwd_state(data, PWD_Confirm_Req);
	else
		eap_pwd_state(data, FAILURE);
}


static void
eap_pwd_process_confirm_resp(struct eap_sm *sm, struct eap_pwd_data *data,
			     const u8 *payload, size_t payload_len)
{
	BIGNUM *x = NULL, *y = NULL;
	HMAC_CTX ctx;
	u32 cs;
	u16 grp;
	u8 conf[SHA256_DIGEST_LENGTH], *cruft = NULL, *ptr;

	/* build up the ciphersuite: group | random_function | prf */
	grp = htons(data->group_num);
	ptr = (u8 *) &cs;
	os_memcpy(ptr, &grp, sizeof(u16));
	ptr += sizeof(u16);
	*ptr = EAP_PWD_DEFAULT_RAND_FUNC;
	ptr += sizeof(u8);
	*ptr = EAP_PWD_DEFAULT_PRF;

	/* each component of the cruft will be at most as big as the prime */
	if (((cruft = os_malloc(BN_num_bytes(data->grp->prime))) == NULL) ||
	    ((x = BN_new()) == NULL) || ((y = BN_new()) == NULL)) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): allocation fail");
		goto fin;
	}

	/*
	 * commit is H(k | peer_element | peer_scalar | server_element |
	 *	       server_scalar | ciphersuite)
	 */
	H_Init(&ctx);

	/* k */
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(data->k, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->prime));

	/* peer element: x, y */
	if (!EC_POINT_get_affine_coordinates_GFp(data->grp->group,
						 data->peer_element, x, y,
						 data->bnctx)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): confirm point "
			   "assignment fail");
		goto fin;
	}
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(x, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->prime));
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(y, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->prime));

	/* peer scalar */
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(data->peer_scalar, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->order));

	/* server element: x, y */
	if (!EC_POINT_get_affine_coordinates_GFp(data->grp->group,
						 data->my_element, x, y,
						 data->bnctx)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): confirm point "
			   "assignment fail");
		goto fin;
	}

	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(x, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->prime));
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(y, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->prime));

	/* server scalar */
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(data->my_scalar, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->order));

	/* ciphersuite */
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	H_Update(&ctx, (u8 *)&cs, sizeof(u32));

	/* all done */
	H_Final(&ctx, conf);

	ptr = (u8 *) payload;
	if (os_memcmp(conf, ptr, SHA256_DIGEST_LENGTH)) {
		wpa_printf(MSG_INFO, "EAP-PWD (server): confirm did not "
			   "verify");
		goto fin;
	}

	wpa_printf(MSG_DEBUG, "EAP-pwd (server): confirm verified");
	if (compute_keys(data->grp, data->bnctx, data->k,
			 data->peer_scalar, data->my_scalar, conf,
			 data->my_confirm, &cs, data->msk, data->emsk) < 0)
		eap_pwd_state(data, FAILURE);
	else
		eap_pwd_state(data, SUCCESS);

fin:
	os_free(cruft);
	BN_free(x);
	BN_free(y);
}


static void eap_pwd_process(struct eap_sm *sm, void *priv,
			    struct wpabuf *respData)
{
	struct eap_pwd_data *data = priv;
	const u8 *pos;
	size_t len;
	u8 exch;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_PWD, respData, &len);
	if ((pos == NULL) || (len < 1)) {
		wpa_printf(MSG_INFO, "Bad EAP header! pos %s and len = %d",
			   (pos == NULL) ? "is NULL" : "is not NULL",
			   (int) len);
		return;
	}

	exch = *pos & 0x3f;
	switch (exch) {
	case EAP_PWD_OPCODE_ID_EXCH:
		eap_pwd_process_id_resp(sm, data, pos + 1, len - 1);
		break;
	case EAP_PWD_OPCODE_COMMIT_EXCH:
		eap_pwd_process_commit_resp(sm, data, pos + 1, len - 1);
		break;
        case EAP_PWD_OPCODE_CONFIRM_EXCH:
		eap_pwd_process_confirm_resp(sm, data, pos + 1, len - 1);
		break;
	}
}


static u8 * eap_pwd_getkey(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_pwd_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_malloc(EAP_MSK_LEN);
	if (key == NULL)
		return NULL;

	os_memcpy(key, data->msk, EAP_MSK_LEN);
	*len = EAP_MSK_LEN;

	return key;
}


static u8 * eap_pwd_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_pwd_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	key = os_malloc(EAP_EMSK_LEN);
	if (key == NULL)
		return NULL;

	os_memcpy(key, data->emsk, EAP_EMSK_LEN);
	*len = EAP_EMSK_LEN;

	return key;
}


static Boolean eap_pwd_is_success(struct eap_sm *sm, void *priv)
{
	struct eap_pwd_data *data = priv;
	return data->state == SUCCESS;
}


static Boolean eap_pwd_is_done(struct eap_sm *sm, void *priv)
{
	struct eap_pwd_data *data = priv;
	return (data->state == SUCCESS) || (data->state == FAILURE);
}


int eap_server_pwd_register(void)
{
	struct eap_method *eap;
	int ret;
	struct timeval tp;
	struct timezone tz;
	u32 sr;

	EVP_add_digest(EVP_sha256());

	sr = 0xdeaddada;
	(void) gettimeofday(&tp, &tz);
	sr ^= (tp.tv_sec ^ tp.tv_usec);
	srandom(sr);

	eap = eap_server_method_alloc(EAP_SERVER_METHOD_INTERFACE_VERSION,
				      EAP_VENDOR_IETF, EAP_TYPE_PWD,
				      "PWD");
	if (eap == NULL)
		return -1;

	eap->init = eap_pwd_init;
	eap->reset = eap_pwd_reset;
	eap->buildReq = eap_pwd_build_req;
	eap->check = eap_pwd_check;
	eap->process = eap_pwd_process;
	eap->isDone = eap_pwd_is_done;
	eap->getKey = eap_pwd_getkey;
	eap->get_emsk = eap_pwd_get_emsk;
	eap->isSuccess = eap_pwd_is_success;

	ret = eap_server_method_register(eap);
	if (ret)
		eap_server_method_free(eap);
	return ret;
}

