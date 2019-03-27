/*
 * RADIUS Dynamic Authorization Server (DAS)
 * Copyright (c) 2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef RADIUS_DAS_H
#define RADIUS_DAS_H

struct radius_das_data;

enum radius_das_res {
	RADIUS_DAS_SUCCESS,
	RADIUS_DAS_NAS_MISMATCH,
	RADIUS_DAS_SESSION_NOT_FOUND,
	RADIUS_DAS_MULTI_SESSION_MATCH,
	RADIUS_DAS_COA_FAILED,
};

struct radius_das_attrs {
	/* NAS identification attributes */
	const u8 *nas_ip_addr;
	const u8 *nas_identifier;
	size_t nas_identifier_len;
	const u8 *nas_ipv6_addr;

	/* Session identification attributes */
	const u8 *sta_addr;
	const u8 *user_name;
	size_t user_name_len;
	const u8 *acct_session_id;
	size_t acct_session_id_len;
	const u8 *acct_multi_session_id;
	size_t acct_multi_session_id_len;
	const u8 *cui;
	size_t cui_len;

	/* Authorization changes */
	const u8 *hs20_t_c_filtering;
};

struct radius_das_conf {
	int port;
	const u8 *shared_secret;
	size_t shared_secret_len;
	const struct hostapd_ip_addr *client_addr;
	unsigned int time_window;
	int require_event_timestamp;
	int require_message_authenticator;
	void *ctx;
	enum radius_das_res (*disconnect)(void *ctx,
					  struct radius_das_attrs *attr);
	enum radius_das_res (*coa)(void *ctx, struct radius_das_attrs *attr);
};

struct radius_das_data *
radius_das_init(struct radius_das_conf *conf);

void radius_das_deinit(struct radius_das_data *data);

#endif /* RADIUS_DAS_H */
