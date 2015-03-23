/*
 * EAP peer method: EAP-pwd (RFC 5931)
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
#include "eap_peer/eap_i.h"
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
	u16 group_num;
	EAP_PWD_group *grp;

	BIGNUM *k;
	BIGNUM *private_value;
	BIGNUM *server_scalar;
	BIGNUM *my_scalar;
	EC_POINT *my_element;
	EC_POINT *server_element;

	u8 msk[EAP_MSK_LEN];
	u8 emsk[EAP_EMSK_LEN];

	BN_CTX *bnctx;
};


#ifndef CONFIG_NO_STDOUT_DEBUG
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
		return "PWD-UNK";
	}
}
#endif  /* CONFIG_NO_STDOUT_DEBUG */


static void eap_pwd_state(struct eap_pwd_data *data, int state)
{
	wpa_printf(MSG_INFO, "EAP-PWD: %s -> %s",
		   eap_pwd_state_txt(data->state), eap_pwd_state_txt(state));
	data->state = state;
}


static void * eap_pwd_init(struct eap_sm *sm)
{
	struct eap_pwd_data *data;
	const u8 *identity, *password;
	size_t identity_len, password_len;

	password = eap_get_config_password(sm, &password_len);
	if (password == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: No password configured!");
		return NULL;
	}

	identity = eap_get_config_identity(sm, &identity_len);
	if (identity == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: No identity configured!");
		return NULL;
	}

	if ((data = os_zalloc(sizeof(*data))) == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: memory allocation data fail");
		return NULL;
	}

	if ((data->bnctx = BN_CTX_new()) == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: bn context allocation fail");
		os_free(data);
		return NULL;
	}

	if ((data->id_peer = os_malloc(identity_len)) == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: memory allocation id fail");
		BN_CTX_free(data->bnctx);
		os_free(data);
		return NULL;
	}

	os_memcpy(data->id_peer, identity, identity_len);
	data->id_peer_len = identity_len;

	if ((data->password = os_malloc(password_len)) == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: memory allocation psk fail");
		BN_CTX_free(data->bnctx);
		os_free(data->id_peer);
		os_free(data);
		return NULL;
	}
	os_memcpy(data->password, password, password_len);
	data->password_len = password_len;

	data->state = PWD_ID_Req;

	return data;
}


static void eap_pwd_deinit(struct eap_sm *sm, void *priv)
{
	struct eap_pwd_data *data = priv;

	BN_free(data->private_value);
	BN_free(data->server_scalar);
	BN_free(data->my_scalar);
	BN_free(data->k);
	BN_CTX_free(data->bnctx);
	EC_POINT_free(data->my_element);
	EC_POINT_free(data->server_element);
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


static struct wpabuf *
eap_pwd_perform_id_exchange(struct eap_sm *sm, struct eap_pwd_data *data,
			    struct eap_method_ret *ret,
			    const struct wpabuf *reqData,
			    const u8 *payload, size_t payload_len)
{
	struct eap_pwd_id *id;
	struct wpabuf *resp;

	if (data->state != PWD_ID_Req) {
		ret->ignore = TRUE;
		return NULL;
	}

	if (payload_len < sizeof(struct eap_pwd_id)) {
		ret->ignore = TRUE;
		return NULL;
	}

	id = (struct eap_pwd_id *) payload;
	data->group_num = be_to_host16(id->group_num);
	if ((id->random_function != EAP_PWD_DEFAULT_RAND_FUNC) ||
	    (id->prf != EAP_PWD_DEFAULT_PRF)) {
		ret->ignore = TRUE;
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "EAP-PWD (peer): server said group %d",
		   data->group_num);

	data->id_server = os_malloc(payload_len - sizeof(struct eap_pwd_id));
	if (data->id_server == NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: memory allocation id fail");
		return NULL;
	}
	data->id_server_len = payload_len - sizeof(struct eap_pwd_id);
	os_memcpy(data->id_server, id->identity, data->id_server_len);
	wpa_hexdump_ascii(MSG_INFO, "EAP-PWD (peer): server sent id of",
			  data->id_server, data->id_server_len);

	if ((data->grp = (EAP_PWD_group *) os_malloc(sizeof(EAP_PWD_group))) ==
	    NULL) {
		wpa_printf(MSG_INFO, "EAP-PWD: failed to allocate memory for "
			   "group");
		return NULL;
	}

	/* compute PWE */
	if (compute_password_element(data->grp, data->group_num,
				     data->password, data->password_len,
				     data->id_server, data->id_server_len,
				     data->id_peer, data->id_peer_len,
				     id->token)) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): unable to compute PWE");
		return NULL;
	}

	wpa_printf(MSG_INFO, "EAP-PWD (peer): computed %d bit PWE...",
		   BN_num_bits(data->grp->prime));

	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PWD,
			     1 + sizeof(struct eap_pwd_id) + data->id_peer_len,
			     EAP_CODE_RESPONSE, eap_get_id(reqData));
	if (resp == NULL)
		return NULL;

	wpabuf_put_u8(resp, EAP_PWD_OPCODE_ID_EXCH);
	wpabuf_put_be16(resp, data->group_num);
	wpabuf_put_u8(resp, EAP_PWD_DEFAULT_RAND_FUNC);
	wpabuf_put_u8(resp, EAP_PWD_DEFAULT_PRF);
	wpabuf_put_data(resp, id->token, sizeof(id->token));
	wpabuf_put_u8(resp, EAP_PWD_PREP_NONE);
	wpabuf_put_data(resp, data->id_peer, data->id_peer_len);

	eap_pwd_state(data, PWD_Commit_Req);

	return resp;
}


static struct wpabuf *
eap_pwd_perform_commit_exchange(struct eap_sm *sm, struct eap_pwd_data *data,
				struct eap_method_ret *ret,
				const struct wpabuf *reqData,
				const u8 *payload, size_t payload_len)
{
	struct wpabuf *resp = NULL;
	EC_POINT *K = NULL, *point = NULL;
	BIGNUM *mask = NULL, *x = NULL, *y = NULL, *cofactor = NULL;
	u16 offset;
	u8 *ptr, *scalar = NULL, *element = NULL;

	if (((data->private_value = BN_new()) == NULL) ||
	    ((data->my_element = EC_POINT_new(data->grp->group)) == NULL) ||
	    ((cofactor = BN_new()) == NULL) ||
	    ((data->my_scalar = BN_new()) == NULL) ||
	    ((mask = BN_new()) == NULL)) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): scalar allocation fail");
		goto fin;
	}

	if (!EC_GROUP_get_cofactor(data->grp->group, cofactor, NULL)) {
		wpa_printf(MSG_INFO, "EAP-pwd (peer): unable to get cofactor "
			   "for curve");
		goto fin;
	}

	BN_rand_range(data->private_value, data->grp->order);
	BN_rand_range(mask, data->grp->order);
	BN_add(data->my_scalar, data->private_value, mask);
	BN_mod(data->my_scalar, data->my_scalar, data->grp->order,
	       data->bnctx);

	if (!EC_POINT_mul(data->grp->group, data->my_element, NULL,
			  data->grp->pwe, mask, data->bnctx)) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): element allocation "
			   "fail");
		eap_pwd_state(data, FAILURE);
		goto fin;
	}

	if (!EC_POINT_invert(data->grp->group, data->my_element, data->bnctx))
	{
		wpa_printf(MSG_INFO, "EAP-PWD (peer): element inversion fail");
		goto fin;
	}
	BN_free(mask);

	if (((x = BN_new()) == NULL) ||
	    ((y = BN_new()) == NULL)) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): point allocation fail");
		goto fin;
	}

	/* process the request */
	if (((data->server_scalar = BN_new()) == NULL) ||
	    ((data->k = BN_new()) == NULL) ||
	    ((K = EC_POINT_new(data->grp->group)) == NULL) ||
	    ((point = EC_POINT_new(data->grp->group)) == NULL) ||
	    ((data->server_element = EC_POINT_new(data->grp->group)) == NULL))
	{
		wpa_printf(MSG_INFO, "EAP-PWD (peer): peer data allocation "
			   "fail");
		goto fin;
	}

	/* element, x then y, followed by scalar */
	ptr = (u8 *) payload;
	BN_bin2bn(ptr, BN_num_bytes(data->grp->prime), x);
	ptr += BN_num_bytes(data->grp->prime);
	BN_bin2bn(ptr, BN_num_bytes(data->grp->prime), y);
	ptr += BN_num_bytes(data->grp->prime);
	BN_bin2bn(ptr, BN_num_bytes(data->grp->order), data->server_scalar);
	if (!EC_POINT_set_affine_coordinates_GFp(data->grp->group,
						 data->server_element, x, y,
						 data->bnctx)) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): setting peer element "
			   "fail");
		goto fin;
	}

	/* check to ensure server's element is not in a small sub-group */
	if (BN_cmp(cofactor, BN_value_one())) {
		if (!EC_POINT_mul(data->grp->group, point, NULL,
				  data->server_element, cofactor, NULL)) {
			wpa_printf(MSG_INFO, "EAP-PWD (peer): cannot multiply "
				   "server element by order!\n");
			goto fin;
		}
		if (EC_POINT_is_at_infinity(data->grp->group, point)) {
			wpa_printf(MSG_INFO, "EAP-PWD (peer): server element "
				   "is at infinity!\n");
			goto fin;
		}
	}

	/* compute the shared key, k */
	if ((!EC_POINT_mul(data->grp->group, K, NULL, data->grp->pwe,
			   data->server_scalar, data->bnctx)) ||
	    (!EC_POINT_add(data->grp->group, K, K, data->server_element,
			   data->bnctx)) ||
	    (!EC_POINT_mul(data->grp->group, K, NULL, K, data->private_value,
			   data->bnctx))) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): computing shared key "
			   "fail");
		goto fin;
	}

	/* ensure that the shared key isn't in a small sub-group */
	if (BN_cmp(cofactor, BN_value_one())) {
		if (!EC_POINT_mul(data->grp->group, K, NULL, K, cofactor,
				  NULL)) {
			wpa_printf(MSG_INFO, "EAP-PWD (peer): cannot multiply "
				   "shared key point by order");
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
		wpa_printf(MSG_INFO, "EAP-PWD (peer): shared key point is at "
			   "infinity!\n");
		goto fin;
	}

	if (!EC_POINT_get_affine_coordinates_GFp(data->grp->group, K, data->k,
						 NULL, data->bnctx)) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): unable to extract "
			   "shared secret from point");
		goto fin;
	}

	/* now do the response */
	if (!EC_POINT_get_affine_coordinates_GFp(data->grp->group,
						 data->my_element, x, y,
						 data->bnctx)) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): point assignment fail");
		goto fin;
	}

	if (((scalar = os_malloc(BN_num_bytes(data->grp->order))) == NULL) ||
	    ((element = os_malloc(BN_num_bytes(data->grp->prime) * 2)) ==
	     NULL)) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): data allocation fail");
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

	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PWD,
			     sizeof(struct eap_pwd_hdr) +
			     BN_num_bytes(data->grp->order) +
			     (2 * BN_num_bytes(data->grp->prime)),
			     EAP_CODE_RESPONSE, eap_get_id(reqData));
	if (resp == NULL)
		goto fin;

	wpabuf_put_u8(resp, EAP_PWD_OPCODE_COMMIT_EXCH);

	/* we send the element as (x,y) follwed by the scalar */
	wpabuf_put_data(resp, element, (2 * BN_num_bytes(data->grp->prime)));
	wpabuf_put_data(resp, scalar, BN_num_bytes(data->grp->order));

fin:
	os_free(scalar);
	os_free(element);
	BN_free(x);
	BN_free(y);
	BN_free(cofactor);
	EC_POINT_free(K);
	EC_POINT_free(point);
	if (resp == NULL)
		eap_pwd_state(data, FAILURE);
	else
		eap_pwd_state(data, PWD_Confirm_Req);

	return resp;
}


static struct wpabuf *
eap_pwd_perform_confirm_exchange(struct eap_sm *sm, struct eap_pwd_data *data,
				 struct eap_method_ret *ret,
				 const struct wpabuf *reqData,
				 const u8 *payload, size_t payload_len)
{
	struct wpabuf *resp = NULL;
	BIGNUM *x = NULL, *y = NULL;
	HMAC_CTX ctx;
	u32 cs;
	u16 grp;
	u8 conf[SHA256_DIGEST_LENGTH], *cruft = NULL, *ptr;

	/*
	 * first build up the ciphersuite which is group | random_function |
	 *	prf
	 */
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
		wpa_printf(MSG_INFO, "EAP-PWD (server): debug allocation "
			   "fail");
		goto fin;
	}

	/*
	 * server's commit is H(k | server_element | server_scalar |
	 *			peer_element | peer_scalar | ciphersuite)
	 */
	H_Init(&ctx);

	/*
	 * zero the memory each time because this is mod prime math and some
	 * value may start with a few zeros and the previous one did not.
	 */
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(data->k, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->prime));

	/* server element: x, y */
	if (!EC_POINT_get_affine_coordinates_GFp(data->grp->group,
						 data->server_element, x, y,
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
	BN_bn2bin(data->server_scalar, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->order));

	/* my element: x, y */
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

	/* my scalar */
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(data->my_scalar, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->order));

	/* the ciphersuite */
	H_Update(&ctx, (u8 *) &cs, sizeof(u32));

	/* random function fin */
	H_Final(&ctx, conf);

	ptr = (u8 *) payload;
	if (os_memcmp(conf, ptr, SHA256_DIGEST_LENGTH)) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): confirm did not verify");
		goto fin;
	}

	wpa_printf(MSG_DEBUG, "EAP-pwd (peer): confirm verified");

	/*
	 * compute confirm:
	 *  H(k | peer_element | peer_scalar | server_element | server_scalar |
	 *    ciphersuite)
	 */
	H_Init(&ctx);

	/* k */
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(data->k, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->prime));

	/* my element */
	if (!EC_POINT_get_affine_coordinates_GFp(data->grp->group,
						 data->my_element, x, y,
						 data->bnctx)) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): confirm point "
			   "assignment fail");
		goto fin;
	}
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(x, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->prime));
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(y, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->prime));

	/* my scalar */
	os_memset(cruft, 0, BN_num_bytes(data->grp->prime));
	BN_bn2bin(data->my_scalar, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->order));

	/* server element: x, y */
	if (!EC_POINT_get_affine_coordinates_GFp(data->grp->group,
						 data->server_element, x, y,
						 data->bnctx)) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): confirm point "
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
	BN_bn2bin(data->server_scalar, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(data->grp->order));

	/* the ciphersuite */
	H_Update(&ctx, (u8 *) &cs, sizeof(u32));

	/* all done */
	H_Final(&ctx, conf);

	resp = eap_msg_alloc(EAP_VENDOR_IETF, EAP_TYPE_PWD,
			     sizeof(struct eap_pwd_hdr) + SHA256_DIGEST_LENGTH,
			     EAP_CODE_RESPONSE, eap_get_id(reqData));
	if (resp == NULL)
		goto fin;

	wpabuf_put_u8(resp, EAP_PWD_OPCODE_CONFIRM_EXCH);
	wpabuf_put_data(resp, conf, SHA256_DIGEST_LENGTH);

	if (compute_keys(data->grp, data->bnctx, data->k,
			 data->my_scalar, data->server_scalar, conf, ptr,
			 &cs, data->msk, data->emsk) < 0) {
		wpa_printf(MSG_INFO, "EAP-PWD (peer): unable to compute MSK | "
			   "EMSK");
		goto fin;
	}

fin:
	os_free(cruft);
	BN_free(x);
	BN_free(y);
	ret->methodState = METHOD_DONE;
	if (resp == NULL) {
		ret->decision = DECISION_FAIL;
		eap_pwd_state(data, FAILURE);
	} else {
		ret->decision = DECISION_UNCOND_SUCC;
		eap_pwd_state(data, SUCCESS);
	}

	return resp;
}


static struct wpabuf *
eap_pwd_process(struct eap_sm *sm, void *priv, struct eap_method_ret *ret,
		const struct wpabuf *reqData)
{
	struct eap_pwd_data *data = priv;
	struct wpabuf *resp = NULL;
	const u8 *pos;
	size_t len;
	u8 exch;

	pos = eap_hdr_validate(EAP_VENDOR_IETF, EAP_TYPE_PWD, reqData, &len);
	if ((pos == NULL) || (len < 1)) {
		ret->ignore = TRUE;
		return NULL;
	}

	wpa_printf(MSG_INFO, "EAP-pwd: Received frame: opcode %d", *pos);

	ret->ignore = FALSE;
	ret->methodState = METHOD_MAY_CONT;
	ret->decision = DECISION_FAIL;
	ret->allowNotifications = FALSE;

	exch = *pos & 0x3f;
	switch (exch) {
        case EAP_PWD_OPCODE_ID_EXCH:
		resp = eap_pwd_perform_id_exchange(sm, data, ret, reqData,
						   pos + 1, len - 1);
		break;
        case EAP_PWD_OPCODE_COMMIT_EXCH:
		resp = eap_pwd_perform_commit_exchange(sm, data, ret, reqData,
						       pos + 1, len - 1);
		break;
        case EAP_PWD_OPCODE_CONFIRM_EXCH:
		resp = eap_pwd_perform_confirm_exchange(sm, data, ret, reqData,
							pos + 1, len - 1);
		break;
        default:
		wpa_printf(MSG_INFO, "EAP-pwd: Ignoring message with unknown "
			   "opcode %d", exch);
		break;
	}

	return resp;
}


static Boolean eap_pwd_key_available(struct eap_sm *sm, void *priv)
{
	struct eap_pwd_data *data = priv;
	return data->state == SUCCESS;
}


static u8 * eap_pwd_get_emsk(struct eap_sm *sm, void *priv, size_t *len)
{
	struct eap_pwd_data *data = priv;
	u8 *key;

	if (data->state != SUCCESS)
		return NULL;

	if ((key = os_malloc(EAP_EMSK_LEN)) == NULL)
		return NULL;

	os_memcpy(key, data->emsk, EAP_EMSK_LEN);
	*len = EAP_EMSK_LEN;

	return key;
}


int eap_peer_pwd_register(void)
{
	struct eap_method *eap;
	int ret;

	EVP_add_digest(EVP_sha256());
	eap = eap_peer_method_alloc(EAP_PEER_METHOD_INTERFACE_VERSION,
				    EAP_VENDOR_IETF, EAP_TYPE_PWD, "PWD");
	if (eap == NULL)
		return -1;

	eap->init = eap_pwd_init;
	eap->deinit = eap_pwd_deinit;
	eap->process = eap_pwd_process;
	eap->isKeyAvailable = eap_pwd_key_available;
	eap->getKey = eap_pwd_getkey;
	eap->get_emsk = eap_pwd_get_emsk;

	ret = eap_peer_method_register(eap);
	if (ret)
		eap_peer_method_free(eap);
	return ret;
}
