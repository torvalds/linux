/*
 * IKEv2 responder (RFC 4306) for EAP-IKEV2
 * Copyright (c) 2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef IKEV2_H
#define IKEV2_H

#include "eap_common/ikev2_common.h"

struct ikev2_proposal_data {
	u8 proposal_num;
	int integ;
	int prf;
	int encr;
	int dh;
};


struct ikev2_responder_data {
	enum { SA_INIT, SA_AUTH, CHILD_SA, NOTIFY, IKEV2_DONE, IKEV2_FAILED }
		state;
	u8 i_spi[IKEV2_SPI_LEN];
	u8 r_spi[IKEV2_SPI_LEN];
	u8 i_nonce[IKEV2_NONCE_MAX_LEN];
	size_t i_nonce_len;
	u8 r_nonce[IKEV2_NONCE_MAX_LEN];
	size_t r_nonce_len;
	struct wpabuf *i_dh_public;
	struct wpabuf *r_dh_private;
	struct ikev2_proposal_data proposal;
	const struct dh_group *dh;
	struct ikev2_keys keys;
	u8 *IDi;
	size_t IDi_len;
	u8 IDi_type;
	u8 *IDr;
	size_t IDr_len;
	struct wpabuf *r_sign_msg;
	struct wpabuf *i_sign_msg;
	u8 *shared_secret;
	size_t shared_secret_len;
	enum { PEER_AUTH_CERT, PEER_AUTH_SECRET } peer_auth;
	u8 *key_pad;
	size_t key_pad_len;
	u16 error_type;
	enum { LAST_MSG_SA_INIT, LAST_MSG_SA_AUTH } last_msg;
};


void ikev2_responder_deinit(struct ikev2_responder_data *data);
int ikev2_responder_process(struct ikev2_responder_data *data,
			    const struct wpabuf *buf);
struct wpabuf * ikev2_responder_build(struct ikev2_responder_data *data);

#endif /* IKEV2_H */
