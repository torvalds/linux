/*
 * RADIUS authentication server
 * Copyright (c) 2005-2009, Jouni Malinen <j@w1.fi>
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
#include <net/if.h>

#include "common.h"
#include "radius.h"
#include "eloop.h"
#include "eap_server/eap.h"
#include "radius_server.h"

/**
 * RADIUS_SESSION_TIMEOUT - Session timeout in seconds
 */
#define RADIUS_SESSION_TIMEOUT 60

/**
 * RADIUS_MAX_SESSION - Maximum number of active sessions
 */
#define RADIUS_MAX_SESSION 100

/**
 * RADIUS_MAX_MSG_LEN - Maximum message length for incoming RADIUS messages
 */
#define RADIUS_MAX_MSG_LEN 3000

static struct eapol_callbacks radius_server_eapol_cb;

struct radius_client;
struct radius_server_data;

/**
 * struct radius_server_counters - RADIUS server statistics counters
 */
struct radius_server_counters {
	u32 access_requests;
	u32 invalid_requests;
	u32 dup_access_requests;
	u32 access_accepts;
	u32 access_rejects;
	u32 access_challenges;
	u32 malformed_access_requests;
	u32 bad_authenticators;
	u32 packets_dropped;
	u32 unknown_types;
};

/**
 * struct radius_session - Internal RADIUS server data for a session
 */
struct radius_session {
	struct radius_session *next;
	struct radius_client *client;
	struct radius_server_data *server;
	unsigned int sess_id;
	struct eap_sm *eap;
	struct eap_eapol_interface *eap_if;

	struct radius_msg *last_msg;
	char *last_from_addr;
	int last_from_port;
	struct sockaddr_storage last_from;
	socklen_t last_fromlen;
	u8 last_identifier;
	struct radius_msg *last_reply;
	u8 last_authenticator[16];
};

/**
 * struct radius_client - Internal RADIUS server data for a client
 */
struct radius_client {
	struct radius_client *next;
	struct in_addr addr;
	struct in_addr mask;
#ifdef CONFIG_IPV6
	struct in6_addr addr6;
	struct in6_addr mask6;
#endif /* CONFIG_IPV6 */
	char *shared_secret;
	int shared_secret_len;
	struct radius_session *sessions;
	struct radius_server_counters counters;
};

/**
 * struct radius_server_data - Internal RADIUS server data
 */
struct radius_server_data {
	/**
	 * auth_sock - Socket for RADIUS authentication messages
	 */
	int auth_sock;

	/**
	 * clients - List of authorized RADIUS clients
	 */
	struct radius_client *clients;

	/**
	 * next_sess_id - Next session identifier
	 */
	unsigned int next_sess_id;

	/**
	 * conf_ctx - Context pointer for callbacks
	 *
	 * This is used as the ctx argument in get_eap_user() calls.
	 */
	void *conf_ctx;

	/**
	 * num_sess - Number of active sessions
	 */
	int num_sess;

	/**
	 * eap_sim_db_priv - EAP-SIM/AKA database context
	 *
	 * This is passed to the EAP-SIM/AKA server implementation as a
	 * callback context.
	 */
	void *eap_sim_db_priv;

	/**
	 * ssl_ctx - TLS context
	 *
	 * This is passed to the EAP server implementation as a callback
	 * context for TLS operations.
	 */
	void *ssl_ctx;

	/**
	 * pac_opaque_encr_key - PAC-Opaque encryption key for EAP-FAST
	 *
	 * This parameter is used to set a key for EAP-FAST to encrypt the
	 * PAC-Opaque data. It can be set to %NULL if EAP-FAST is not used. If
	 * set, must point to a 16-octet key.
	 */
	u8 *pac_opaque_encr_key;

	/**
	 * eap_fast_a_id - EAP-FAST authority identity (A-ID)
	 *
	 * If EAP-FAST is not used, this can be set to %NULL. In theory, this
	 * is a variable length field, but due to some existing implementations
	 * requiring A-ID to be 16 octets in length, it is recommended to use
	 * that length for the field to provide interoperability with deployed
	 * peer implementations.
	 */
	u8 *eap_fast_a_id;

	/**
	 * eap_fast_a_id_len - Length of eap_fast_a_id buffer in octets
	 */
	size_t eap_fast_a_id_len;

	/**
	 * eap_fast_a_id_info - EAP-FAST authority identifier information
	 *
	 * This A-ID-Info contains a user-friendly name for the A-ID. For
	 * example, this could be the enterprise and server names in
	 * human-readable format. This field is encoded as UTF-8. If EAP-FAST
	 * is not used, this can be set to %NULL.
	 */
	char *eap_fast_a_id_info;

	/**
	 * eap_fast_prov - EAP-FAST provisioning modes
	 *
	 * 0 = provisioning disabled, 1 = only anonymous provisioning allowed,
	 * 2 = only authenticated provisioning allowed, 3 = both provisioning
	 * modes allowed.
	 */
	int eap_fast_prov;

	/**
	 * pac_key_lifetime - EAP-FAST PAC-Key lifetime in seconds
	 *
	 * This is the hard limit on how long a provisioned PAC-Key can be
	 * used.
	 */
	int pac_key_lifetime;

	/**
	 * pac_key_refresh_time - EAP-FAST PAC-Key refresh time in seconds
	 *
	 * This is a soft limit on the PAC-Key. The server will automatically
	 * generate a new PAC-Key when this number of seconds (or fewer) of the
	 * lifetime remains.
	 */
	int pac_key_refresh_time;

	/**
	 * eap_sim_aka_result_ind - EAP-SIM/AKA protected success indication
	 *
	 * This controls whether the protected success/failure indication
	 * (AT_RESULT_IND) is used with EAP-SIM and EAP-AKA.
	 */
	int eap_sim_aka_result_ind;

	/**
	 * tnc - Trusted Network Connect (TNC)
	 *
	 * This controls whether TNC is enabled and will be required before the
	 * peer is allowed to connect. Note: This is only used with EAP-TTLS
	 * and EAP-FAST. If any other EAP method is enabled, the peer will be
	 * allowed to connect without TNC.
	 */
	int tnc;

	/**
	 * pwd_group - The D-H group assigned for EAP-pwd
	 *
	 * If EAP-pwd is not used it can be set to zero.
	 */
	u16 pwd_group;

	/**
	 * wps - Wi-Fi Protected Setup context
	 *
	 * If WPS is used with an external RADIUS server (which is quite
	 * unlikely configuration), this is used to provide a pointer to WPS
	 * context data. Normally, this can be set to %NULL.
	 */
	struct wps_context *wps;

	/**
	 * ipv6 - Whether to enable IPv6 support in the RADIUS server
	 */
	int ipv6;

	/**
	 * start_time - Timestamp of server start
	 */
	struct os_time start_time;

	/**
	 * counters - Statistics counters for server operations
	 *
	 * These counters are the sum over all clients.
	 */
	struct radius_server_counters counters;

	/**
	 * get_eap_user - Callback for fetching EAP user information
	 * @ctx: Context data from conf_ctx
	 * @identity: User identity
	 * @identity_len: identity buffer length in octets
	 * @phase2: Whether this is for Phase 2 identity
	 * @user: Data structure for filling in the user information
	 * Returns: 0 on success, -1 on failure
	 *
	 * This is used to fetch information from user database. The callback
	 * will fill in information about allowed EAP methods and the user
	 * password. The password field will be an allocated copy of the
	 * password data and RADIUS server will free it after use.
	 */
	int (*get_eap_user)(void *ctx, const u8 *identity, size_t identity_len,
			    int phase2, struct eap_user *user);

	/**
	 * eap_req_id_text - Optional data for EAP-Request/Identity
	 *
	 * This can be used to configure an optional, displayable message that
	 * will be sent in EAP-Request/Identity. This string can contain an
	 * ASCII-0 character (nul) to separate network infromation per RFC
	 * 4284. The actual string length is explicit provided in
	 * eap_req_id_text_len since nul character will not be used as a string
	 * terminator.
	 */
	char *eap_req_id_text;

	/**
	 * eap_req_id_text_len - Length of eap_req_id_text buffer in octets
	 */
	size_t eap_req_id_text_len;

	/*
	 * msg_ctx - Context data for wpa_msg() calls
	 */
	void *msg_ctx;
};


extern int wpa_debug_level;

#define RADIUS_DEBUG(args...) \
wpa_printf(MSG_DEBUG, "RADIUS SRV: " args)
#define RADIUS_ERROR(args...) \
wpa_printf(MSG_ERROR, "RADIUS SRV: " args)
#define RADIUS_DUMP(args...) \
wpa_hexdump(MSG_MSGDUMP, "RADIUS SRV: " args)
#define RADIUS_DUMP_ASCII(args...) \
wpa_hexdump_ascii(MSG_MSGDUMP, "RADIUS SRV: " args)


static void radius_server_session_timeout(void *eloop_ctx, void *timeout_ctx);
static void radius_server_session_remove_timeout(void *eloop_ctx,
						 void *timeout_ctx);


static struct radius_client *
radius_server_get_client(struct radius_server_data *data, struct in_addr *addr,
			 int ipv6)
{
	struct radius_client *client = data->clients;

	while (client) {
#ifdef CONFIG_IPV6
		if (ipv6) {
			struct in6_addr *addr6;
			int i;

			addr6 = (struct in6_addr *) addr;
			for (i = 0; i < 16; i++) {
				if ((addr6->s6_addr[i] &
				     client->mask6.s6_addr[i]) !=
				    (client->addr6.s6_addr[i] &
				     client->mask6.s6_addr[i])) {
					i = 17;
					break;
				}
			}
			if (i == 16) {
				break;
			}
		}
#endif /* CONFIG_IPV6 */
		if (!ipv6 && (client->addr.s_addr & client->mask.s_addr) ==
		    (addr->s_addr & client->mask.s_addr)) {
			break;
		}

		client = client->next;
	}

	return client;
}


static struct radius_session *
radius_server_get_session(struct radius_client *client, unsigned int sess_id)
{
	struct radius_session *sess = client->sessions;

	while (sess) {
		if (sess->sess_id == sess_id) {
			break;
		}
		sess = sess->next;
	}

	return sess;
}


static void radius_server_session_free(struct radius_server_data *data,
				       struct radius_session *sess)
{
	eloop_cancel_timeout(radius_server_session_timeout, data, sess);
	eloop_cancel_timeout(radius_server_session_remove_timeout, data, sess);
	eap_server_sm_deinit(sess->eap);
	radius_msg_free(sess->last_msg);
	os_free(sess->last_from_addr);
	radius_msg_free(sess->last_reply);
	os_free(sess);
	data->num_sess--;
}


static void radius_server_session_remove(struct radius_server_data *data,
					 struct radius_session *sess)
{
	struct radius_client *client = sess->client;
	struct radius_session *session, *prev;

	eloop_cancel_timeout(radius_server_session_remove_timeout, data, sess);

	prev = NULL;
	session = client->sessions;
	while (session) {
		if (session == sess) {
			if (prev == NULL) {
				client->sessions = sess->next;
			} else {
				prev->next = sess->next;
			}
			radius_server_session_free(data, sess);
			break;
		}
		prev = session;
		session = session->next;
	}
}


static void radius_server_session_remove_timeout(void *eloop_ctx,
						 void *timeout_ctx)
{
	struct radius_server_data *data = eloop_ctx;
	struct radius_session *sess = timeout_ctx;
	RADIUS_DEBUG("Removing completed session 0x%x", sess->sess_id);
	radius_server_session_remove(data, sess);
}


static void radius_server_session_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct radius_server_data *data = eloop_ctx;
	struct radius_session *sess = timeout_ctx;

	RADIUS_DEBUG("Timing out authentication session 0x%x", sess->sess_id);
	radius_server_session_remove(data, sess);
}


static struct radius_session *
radius_server_new_session(struct radius_server_data *data,
			  struct radius_client *client)
{
	struct radius_session *sess;

	if (data->num_sess >= RADIUS_MAX_SESSION) {
		RADIUS_DEBUG("Maximum number of existing session - no room "
			     "for a new session");
		return NULL;
	}

	sess = os_zalloc(sizeof(*sess));
	if (sess == NULL)
		return NULL;

	sess->server = data;
	sess->client = client;
	sess->sess_id = data->next_sess_id++;
	sess->next = client->sessions;
	client->sessions = sess;
	eloop_register_timeout(RADIUS_SESSION_TIMEOUT, 0,
			       radius_server_session_timeout, data, sess);
	data->num_sess++;
	return sess;
}


static struct radius_session *
radius_server_get_new_session(struct radius_server_data *data,
			      struct radius_client *client,
			      struct radius_msg *msg)
{
	u8 *user;
	size_t user_len;
	int res;
	struct radius_session *sess;
	struct eap_config eap_conf;

	RADIUS_DEBUG("Creating a new session");

	user = os_malloc(256);
	if (user == NULL) {
		return NULL;
	}
	res = radius_msg_get_attr(msg, RADIUS_ATTR_USER_NAME, user, 256);
	if (res < 0 || res > 256) {
		RADIUS_DEBUG("Could not get User-Name");
		os_free(user);
		return NULL;
	}
	user_len = res;
	RADIUS_DUMP_ASCII("User-Name", user, user_len);

	res = data->get_eap_user(data->conf_ctx, user, user_len, 0, NULL);
	os_free(user);

	if (res == 0) {
		RADIUS_DEBUG("Matching user entry found");
		sess = radius_server_new_session(data, client);
		if (sess == NULL) {
			RADIUS_DEBUG("Failed to create a new session");
			return NULL;
		}
	} else {
		RADIUS_DEBUG("User-Name not found from user database");
		return NULL;
	}

	os_memset(&eap_conf, 0, sizeof(eap_conf));
	eap_conf.ssl_ctx = data->ssl_ctx;
	eap_conf.msg_ctx = data->msg_ctx;
	eap_conf.eap_sim_db_priv = data->eap_sim_db_priv;
	eap_conf.backend_auth = TRUE;
	eap_conf.eap_server = 1;
	eap_conf.pac_opaque_encr_key = data->pac_opaque_encr_key;
	eap_conf.eap_fast_a_id = data->eap_fast_a_id;
	eap_conf.eap_fast_a_id_len = data->eap_fast_a_id_len;
	eap_conf.eap_fast_a_id_info = data->eap_fast_a_id_info;
	eap_conf.eap_fast_prov = data->eap_fast_prov;
	eap_conf.pac_key_lifetime = data->pac_key_lifetime;
	eap_conf.pac_key_refresh_time = data->pac_key_refresh_time;
	eap_conf.eap_sim_aka_result_ind = data->eap_sim_aka_result_ind;
	eap_conf.tnc = data->tnc;
	eap_conf.wps = data->wps;
	eap_conf.pwd_group = data->pwd_group;
	sess->eap = eap_server_sm_init(sess, &radius_server_eapol_cb,
				       &eap_conf);
	if (sess->eap == NULL) {
		RADIUS_DEBUG("Failed to initialize EAP state machine for the "
			     "new session");
		radius_server_session_free(data, sess);
		return NULL;
	}
	sess->eap_if = eap_get_interface(sess->eap);
	sess->eap_if->eapRestart = TRUE;
	sess->eap_if->portEnabled = TRUE;

	RADIUS_DEBUG("New session 0x%x initialized", sess->sess_id);

	return sess;
}


static struct radius_msg *
radius_server_encapsulate_eap(struct radius_server_data *data,
			      struct radius_client *client,
			      struct radius_session *sess,
			      struct radius_msg *request)
{
	struct radius_msg *msg;
	int code;
	unsigned int sess_id;
	struct radius_hdr *hdr = radius_msg_get_hdr(request);

	if (sess->eap_if->eapFail) {
		sess->eap_if->eapFail = FALSE;
		code = RADIUS_CODE_ACCESS_REJECT;
	} else if (sess->eap_if->eapSuccess) {
		sess->eap_if->eapSuccess = FALSE;
		code = RADIUS_CODE_ACCESS_ACCEPT;
	} else {
		sess->eap_if->eapReq = FALSE;
		code = RADIUS_CODE_ACCESS_CHALLENGE;
	}

	msg = radius_msg_new(code, hdr->identifier);
	if (msg == NULL) {
		RADIUS_DEBUG("Failed to allocate reply message");
		return NULL;
	}

	sess_id = htonl(sess->sess_id);
	if (code == RADIUS_CODE_ACCESS_CHALLENGE &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_STATE,
				 (u8 *) &sess_id, sizeof(sess_id))) {
		RADIUS_DEBUG("Failed to add State attribute");
	}

	if (sess->eap_if->eapReqData &&
	    !radius_msg_add_eap(msg, wpabuf_head(sess->eap_if->eapReqData),
				wpabuf_len(sess->eap_if->eapReqData))) {
		RADIUS_DEBUG("Failed to add EAP-Message attribute");
	}

	if (code == RADIUS_CODE_ACCESS_ACCEPT && sess->eap_if->eapKeyData) {
		int len;
		if (sess->eap_if->eapKeyDataLen > 64) {
			len = 32;
		} else {
			len = sess->eap_if->eapKeyDataLen / 2;
		}
		if (!radius_msg_add_mppe_keys(msg, hdr->authenticator,
					      (u8 *) client->shared_secret,
					      client->shared_secret_len,
					      sess->eap_if->eapKeyData + len,
					      len, sess->eap_if->eapKeyData,
					      len)) {
			RADIUS_DEBUG("Failed to add MPPE key attributes");
		}
	}

	if (radius_msg_copy_attr(msg, request, RADIUS_ATTR_PROXY_STATE) < 0) {
		RADIUS_DEBUG("Failed to copy Proxy-State attribute(s)");
		radius_msg_free(msg);
		return NULL;
	}

	if (radius_msg_finish_srv(msg, (u8 *) client->shared_secret,
				  client->shared_secret_len,
				  hdr->authenticator) < 0) {
		RADIUS_DEBUG("Failed to add Message-Authenticator attribute");
	}

	return msg;
}


static int radius_server_reject(struct radius_server_data *data,
				struct radius_client *client,
				struct radius_msg *request,
				struct sockaddr *from, socklen_t fromlen,
				const char *from_addr, int from_port)
{
	struct radius_msg *msg;
	int ret = 0;
	struct eap_hdr eapfail;
	struct wpabuf *buf;
	struct radius_hdr *hdr = radius_msg_get_hdr(request);

	RADIUS_DEBUG("Reject invalid request from %s:%d",
		     from_addr, from_port);

	msg = radius_msg_new(RADIUS_CODE_ACCESS_REJECT, hdr->identifier);
	if (msg == NULL) {
		return -1;
	}

	os_memset(&eapfail, 0, sizeof(eapfail));
	eapfail.code = EAP_CODE_FAILURE;
	eapfail.identifier = 0;
	eapfail.length = host_to_be16(sizeof(eapfail));

	if (!radius_msg_add_eap(msg, (u8 *) &eapfail, sizeof(eapfail))) {
		RADIUS_DEBUG("Failed to add EAP-Message attribute");
	}

	if (radius_msg_copy_attr(msg, request, RADIUS_ATTR_PROXY_STATE) < 0) {
		RADIUS_DEBUG("Failed to copy Proxy-State attribute(s)");
		radius_msg_free(msg);
		return -1;
	}

	if (radius_msg_finish_srv(msg, (u8 *) client->shared_secret,
				  client->shared_secret_len,
				  hdr->authenticator) <
	    0) {
		RADIUS_DEBUG("Failed to add Message-Authenticator attribute");
	}

	if (wpa_debug_level <= MSG_MSGDUMP) {
		radius_msg_dump(msg);
	}

	data->counters.access_rejects++;
	client->counters.access_rejects++;
	buf = radius_msg_get_buf(msg);
	if (sendto(data->auth_sock, wpabuf_head(buf), wpabuf_len(buf), 0,
		   (struct sockaddr *) from, sizeof(*from)) < 0) {
		perror("sendto[RADIUS SRV]");
		ret = -1;
	}

	radius_msg_free(msg);

	return ret;
}


static int radius_server_request(struct radius_server_data *data,
				 struct radius_msg *msg,
				 struct sockaddr *from, socklen_t fromlen,
				 struct radius_client *client,
				 const char *from_addr, int from_port,
				 struct radius_session *force_sess)
{
	u8 *eap = NULL;
	size_t eap_len;
	int res, state_included = 0;
	u8 statebuf[4];
	unsigned int state;
	struct radius_session *sess;
	struct radius_msg *reply;
	int is_complete = 0;

	if (force_sess)
		sess = force_sess;
	else {
		res = radius_msg_get_attr(msg, RADIUS_ATTR_STATE, statebuf,
					  sizeof(statebuf));
		state_included = res >= 0;
		if (res == sizeof(statebuf)) {
			state = WPA_GET_BE32(statebuf);
			sess = radius_server_get_session(client, state);
		} else {
			sess = NULL;
		}
	}

	if (sess) {
		RADIUS_DEBUG("Request for session 0x%x", sess->sess_id);
	} else if (state_included) {
		RADIUS_DEBUG("State attribute included but no session found");
		radius_server_reject(data, client, msg, from, fromlen,
				     from_addr, from_port);
		return -1;
	} else {
		sess = radius_server_get_new_session(data, client, msg);
		if (sess == NULL) {
			RADIUS_DEBUG("Could not create a new session");
			radius_server_reject(data, client, msg, from, fromlen,
					     from_addr, from_port);
			return -1;
		}
	}

	if (sess->last_from_port == from_port &&
	    sess->last_identifier == radius_msg_get_hdr(msg)->identifier &&
	    os_memcmp(sess->last_authenticator,
		      radius_msg_get_hdr(msg)->authenticator, 16) == 0) {
		RADIUS_DEBUG("Duplicate message from %s", from_addr);
		data->counters.dup_access_requests++;
		client->counters.dup_access_requests++;

		if (sess->last_reply) {
			struct wpabuf *buf;
			buf = radius_msg_get_buf(sess->last_reply);
			res = sendto(data->auth_sock, wpabuf_head(buf),
				     wpabuf_len(buf), 0,
				     (struct sockaddr *) from, fromlen);
			if (res < 0) {
				perror("sendto[RADIUS SRV]");
			}
			return 0;
		}

		RADIUS_DEBUG("No previous reply available for duplicate "
			     "message");
		return -1;
	}
		      
	eap = radius_msg_get_eap(msg, &eap_len);
	if (eap == NULL) {
		RADIUS_DEBUG("No EAP-Message in RADIUS packet from %s",
			     from_addr);
		data->counters.packets_dropped++;
		client->counters.packets_dropped++;
		return -1;
	}

	RADIUS_DUMP("Received EAP data", eap, eap_len);

	/* FIX: if Code is Request, Success, or Failure, send Access-Reject;
	 * RFC3579 Sect. 2.6.2.
	 * Include EAP-Response/Nak with no preferred method if
	 * code == request.
	 * If code is not 1-4, discard the packet silently.
	 * Or is this already done by the EAP state machine? */

	wpabuf_free(sess->eap_if->eapRespData);
	sess->eap_if->eapRespData = wpabuf_alloc_ext_data(eap, eap_len);
	if (sess->eap_if->eapRespData == NULL)
		os_free(eap);
	eap = NULL;
	sess->eap_if->eapResp = TRUE;
	eap_server_sm_step(sess->eap);

	if ((sess->eap_if->eapReq || sess->eap_if->eapSuccess ||
	     sess->eap_if->eapFail) && sess->eap_if->eapReqData) {
		RADIUS_DUMP("EAP data from the state machine",
			    wpabuf_head(sess->eap_if->eapReqData),
			    wpabuf_len(sess->eap_if->eapReqData));
	} else if (sess->eap_if->eapFail) {
		RADIUS_DEBUG("No EAP data from the state machine, but eapFail "
			     "set");
	} else if (eap_sm_method_pending(sess->eap)) {
		radius_msg_free(sess->last_msg);
		sess->last_msg = msg;
		sess->last_from_port = from_port;
		os_free(sess->last_from_addr);
		sess->last_from_addr = os_strdup(from_addr);
		sess->last_fromlen = fromlen;
		os_memcpy(&sess->last_from, from, fromlen);
		return -2;
	} else {
		RADIUS_DEBUG("No EAP data from the state machine - ignore this"
			     " Access-Request silently (assuming it was a "
			     "duplicate)");
		data->counters.packets_dropped++;
		client->counters.packets_dropped++;
		return -1;
	}

	if (sess->eap_if->eapSuccess || sess->eap_if->eapFail)
		is_complete = 1;

	reply = radius_server_encapsulate_eap(data, client, sess, msg);

	if (reply) {
		struct wpabuf *buf;
		struct radius_hdr *hdr;

		RADIUS_DEBUG("Reply to %s:%d", from_addr, from_port);
		if (wpa_debug_level <= MSG_MSGDUMP) {
			radius_msg_dump(reply);
		}

		switch (radius_msg_get_hdr(reply)->code) {
		case RADIUS_CODE_ACCESS_ACCEPT:
			data->counters.access_accepts++;
			client->counters.access_accepts++;
			break;
		case RADIUS_CODE_ACCESS_REJECT:
			data->counters.access_rejects++;
			client->counters.access_rejects++;
			break;
		case RADIUS_CODE_ACCESS_CHALLENGE:
			data->counters.access_challenges++;
			client->counters.access_challenges++;
			break;
		}
		buf = radius_msg_get_buf(reply);
		res = sendto(data->auth_sock, wpabuf_head(buf),
			     wpabuf_len(buf), 0,
			     (struct sockaddr *) from, fromlen);
		if (res < 0) {
			perror("sendto[RADIUS SRV]");
		}
		radius_msg_free(sess->last_reply);
		sess->last_reply = reply;
		sess->last_from_port = from_port;
		hdr = radius_msg_get_hdr(msg);
		sess->last_identifier = hdr->identifier;
		os_memcpy(sess->last_authenticator, hdr->authenticator, 16);
	} else {
		data->counters.packets_dropped++;
		client->counters.packets_dropped++;
	}

	if (is_complete) {
		RADIUS_DEBUG("Removing completed session 0x%x after timeout",
			     sess->sess_id);
		eloop_cancel_timeout(radius_server_session_remove_timeout,
				     data, sess);
		eloop_register_timeout(10, 0,
				       radius_server_session_remove_timeout,
				       data, sess);
	}

	return 0;
}


static void radius_server_receive_auth(int sock, void *eloop_ctx,
				       void *sock_ctx)
{
	struct radius_server_data *data = eloop_ctx;
	u8 *buf = NULL;
	union {
		struct sockaddr_storage ss;
		struct sockaddr_in sin;
#ifdef CONFIG_IPV6
		struct sockaddr_in6 sin6;
#endif /* CONFIG_IPV6 */
	} from;
	socklen_t fromlen;
	int len;
	struct radius_client *client = NULL;
	struct radius_msg *msg = NULL;
	char abuf[50];
	int from_port = 0;

	buf = os_malloc(RADIUS_MAX_MSG_LEN);
	if (buf == NULL) {
		goto fail;
	}

	fromlen = sizeof(from);
	len = recvfrom(sock, buf, RADIUS_MAX_MSG_LEN, 0,
		       (struct sockaddr *) &from.ss, &fromlen);
	if (len < 0) {
		perror("recvfrom[radius_server]");
		goto fail;
	}

#ifdef CONFIG_IPV6
	if (data->ipv6) {
		if (inet_ntop(AF_INET6, &from.sin6.sin6_addr, abuf,
			      sizeof(abuf)) == NULL)
			abuf[0] = '\0';
		from_port = ntohs(from.sin6.sin6_port);
		RADIUS_DEBUG("Received %d bytes from %s:%d",
			     len, abuf, from_port);

		client = radius_server_get_client(data,
						  (struct in_addr *)
						  &from.sin6.sin6_addr, 1);
	}
#endif /* CONFIG_IPV6 */

	if (!data->ipv6) {
		os_strlcpy(abuf, inet_ntoa(from.sin.sin_addr), sizeof(abuf));
		from_port = ntohs(from.sin.sin_port);
		RADIUS_DEBUG("Received %d bytes from %s:%d",
			     len, abuf, from_port);

		client = radius_server_get_client(data, &from.sin.sin_addr, 0);
	}

	RADIUS_DUMP("Received data", buf, len);

	if (client == NULL) {
		RADIUS_DEBUG("Unknown client %s - packet ignored", abuf);
		data->counters.invalid_requests++;
		goto fail;
	}

	msg = radius_msg_parse(buf, len);
	if (msg == NULL) {
		RADIUS_DEBUG("Parsing incoming RADIUS frame failed");
		data->counters.malformed_access_requests++;
		client->counters.malformed_access_requests++;
		goto fail;
	}

	os_free(buf);
	buf = NULL;

	if (wpa_debug_level <= MSG_MSGDUMP) {
		radius_msg_dump(msg);
	}

	if (radius_msg_get_hdr(msg)->code != RADIUS_CODE_ACCESS_REQUEST) {
		RADIUS_DEBUG("Unexpected RADIUS code %d",
			     radius_msg_get_hdr(msg)->code);
		data->counters.unknown_types++;
		client->counters.unknown_types++;
		goto fail;
	}

	data->counters.access_requests++;
	client->counters.access_requests++;

	if (radius_msg_verify_msg_auth(msg, (u8 *) client->shared_secret,
				       client->shared_secret_len, NULL)) {
		RADIUS_DEBUG("Invalid Message-Authenticator from %s", abuf);
		data->counters.bad_authenticators++;
		client->counters.bad_authenticators++;
		goto fail;
	}

	if (radius_server_request(data, msg, (struct sockaddr *) &from,
				  fromlen, client, abuf, from_port, NULL) ==
	    -2)
		return; /* msg was stored with the session */

fail:
	radius_msg_free(msg);
	os_free(buf);
}


static int radius_server_disable_pmtu_discovery(int s)
{
	int r = -1;
#if defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_DONT)
	/* Turn off Path MTU discovery on IPv4/UDP sockets. */
	int action = IP_PMTUDISC_DONT;
	r = setsockopt(s, IPPROTO_IP, IP_MTU_DISCOVER, &action,
		       sizeof(action));
	if (r == -1)
		wpa_printf(MSG_ERROR, "Failed to set IP_MTU_DISCOVER: "
			   "%s", strerror(errno));
#endif
	return r;
}


static int radius_server_open_socket(int port)
{
	int s;
	struct sockaddr_in addr;

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		perror("socket");
		return -1;
	}

	radius_server_disable_pmtu_discovery(s);

	os_memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("bind");
		close(s);
		return -1;
	}

	return s;
}


#ifdef CONFIG_IPV6
static int radius_server_open_socket6(int port)
{
	int s;
	struct sockaddr_in6 addr;

	s = socket(PF_INET6, SOCK_DGRAM, 0);
	if (s < 0) {
		perror("socket[IPv6]");
		return -1;
	}

	os_memset(&addr, 0, sizeof(addr));
	addr.sin6_family = AF_INET6;
	os_memcpy(&addr.sin6_addr, &in6addr_any, sizeof(in6addr_any));
	addr.sin6_port = htons(port);
	if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("bind");
		close(s);
		return -1;
	}

	return s;
}
#endif /* CONFIG_IPV6 */


static void radius_server_free_sessions(struct radius_server_data *data,
					struct radius_session *sessions)
{
	struct radius_session *session, *prev;

	session = sessions;
	while (session) {
		prev = session;
		session = session->next;
		radius_server_session_free(data, prev);
	}
}


static void radius_server_free_clients(struct radius_server_data *data,
				       struct radius_client *clients)
{
	struct radius_client *client, *prev;

	client = clients;
	while (client) {
		prev = client;
		client = client->next;

		radius_server_free_sessions(data, prev->sessions);
		os_free(prev->shared_secret);
		os_free(prev);
	}
}


static struct radius_client *
radius_server_read_clients(const char *client_file, int ipv6)
{
	FILE *f;
	const int buf_size = 1024;
	char *buf, *pos;
	struct radius_client *clients, *tail, *entry;
	int line = 0, mask, failed = 0, i;
	struct in_addr addr;
#ifdef CONFIG_IPV6
	struct in6_addr addr6;
#endif /* CONFIG_IPV6 */
	unsigned int val;

	f = fopen(client_file, "r");
	if (f == NULL) {
		RADIUS_ERROR("Could not open client file '%s'", client_file);
		return NULL;
	}

	buf = os_malloc(buf_size);
	if (buf == NULL) {
		fclose(f);
		return NULL;
	}

	clients = tail = NULL;
	while (fgets(buf, buf_size, f)) {
		/* Configuration file format:
		 * 192.168.1.0/24 secret
		 * 192.168.1.2 secret
		 * fe80::211:22ff:fe33:4455/64 secretipv6
		 */
		line++;
		buf[buf_size - 1] = '\0';
		pos = buf;
		while (*pos != '\0' && *pos != '\n')
			pos++;
		if (*pos == '\n')
			*pos = '\0';
		if (*buf == '\0' || *buf == '#')
			continue;

		pos = buf;
		while ((*pos >= '0' && *pos <= '9') || *pos == '.' ||
		       (*pos >= 'a' && *pos <= 'f') || *pos == ':' ||
		       (*pos >= 'A' && *pos <= 'F')) {
			pos++;
		}

		if (*pos == '\0') {
			failed = 1;
			break;
		}

		if (*pos == '/') {
			char *end;
			*pos++ = '\0';
			mask = strtol(pos, &end, 10);
			if ((pos == end) ||
			    (mask < 0 || mask > (ipv6 ? 128 : 32))) {
				failed = 1;
				break;
			}
			pos = end;
		} else {
			mask = ipv6 ? 128 : 32;
			*pos++ = '\0';
		}

		if (!ipv6 && inet_aton(buf, &addr) == 0) {
			failed = 1;
			break;
		}
#ifdef CONFIG_IPV6
		if (ipv6 && inet_pton(AF_INET6, buf, &addr6) <= 0) {
			if (inet_pton(AF_INET, buf, &addr) <= 0) {
				failed = 1;
				break;
			}
			/* Convert IPv4 address to IPv6 */
			if (mask <= 32)
				mask += (128 - 32);
			os_memset(addr6.s6_addr, 0, 10);
			addr6.s6_addr[10] = 0xff;
			addr6.s6_addr[11] = 0xff;
			os_memcpy(addr6.s6_addr + 12, (char *) &addr.s_addr,
				  4);
		}
#endif /* CONFIG_IPV6 */

		while (*pos == ' ' || *pos == '\t') {
			pos++;
		}

		if (*pos == '\0') {
			failed = 1;
			break;
		}

		entry = os_zalloc(sizeof(*entry));
		if (entry == NULL) {
			failed = 1;
			break;
		}
		entry->shared_secret = os_strdup(pos);
		if (entry->shared_secret == NULL) {
			failed = 1;
			os_free(entry);
			break;
		}
		entry->shared_secret_len = os_strlen(entry->shared_secret);
		entry->addr.s_addr = addr.s_addr;
		if (!ipv6) {
			val = 0;
			for (i = 0; i < mask; i++)
				val |= 1 << (31 - i);
			entry->mask.s_addr = htonl(val);
		}
#ifdef CONFIG_IPV6
		if (ipv6) {
			int offset = mask / 8;

			os_memcpy(entry->addr6.s6_addr, addr6.s6_addr, 16);
			os_memset(entry->mask6.s6_addr, 0xff, offset);
			val = 0;
			for (i = 0; i < (mask % 8); i++)
				val |= 1 << (7 - i);
			if (offset < 16)
				entry->mask6.s6_addr[offset] = val;
		}
#endif /* CONFIG_IPV6 */

		if (tail == NULL) {
			clients = tail = entry;
		} else {
			tail->next = entry;
			tail = entry;
		}
	}

	if (failed) {
		RADIUS_ERROR("Invalid line %d in '%s'", line, client_file);
		radius_server_free_clients(NULL, clients);
		clients = NULL;
	}

	os_free(buf);
	fclose(f);

	return clients;
}


/**
 * radius_server_init - Initialize RADIUS server
 * @conf: Configuration for the RADIUS server
 * Returns: Pointer to private RADIUS server context or %NULL on failure
 *
 * This initializes a RADIUS server instance and returns a context pointer that
 * will be used in other calls to the RADIUS server module. The server can be
 * deinitialize by calling radius_server_deinit().
 */
struct radius_server_data *
radius_server_init(struct radius_server_conf *conf)
{
	struct radius_server_data *data;

#ifndef CONFIG_IPV6
	if (conf->ipv6) {
		fprintf(stderr, "RADIUS server compiled without IPv6 "
			"support.\n");
		return NULL;
	}
#endif /* CONFIG_IPV6 */

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;

	os_get_time(&data->start_time);
	data->conf_ctx = conf->conf_ctx;
	data->eap_sim_db_priv = conf->eap_sim_db_priv;
	data->ssl_ctx = conf->ssl_ctx;
	data->msg_ctx = conf->msg_ctx;
	data->ipv6 = conf->ipv6;
	if (conf->pac_opaque_encr_key) {
		data->pac_opaque_encr_key = os_malloc(16);
		os_memcpy(data->pac_opaque_encr_key, conf->pac_opaque_encr_key,
			  16);
	}
	if (conf->eap_fast_a_id) {
		data->eap_fast_a_id = os_malloc(conf->eap_fast_a_id_len);
		if (data->eap_fast_a_id) {
			os_memcpy(data->eap_fast_a_id, conf->eap_fast_a_id,
				  conf->eap_fast_a_id_len);
			data->eap_fast_a_id_len = conf->eap_fast_a_id_len;
		}
	}
	if (conf->eap_fast_a_id_info)
		data->eap_fast_a_id_info = os_strdup(conf->eap_fast_a_id_info);
	data->eap_fast_prov = conf->eap_fast_prov;
	data->pac_key_lifetime = conf->pac_key_lifetime;
	data->pac_key_refresh_time = conf->pac_key_refresh_time;
	data->get_eap_user = conf->get_eap_user;
	data->eap_sim_aka_result_ind = conf->eap_sim_aka_result_ind;
	data->tnc = conf->tnc;
	data->wps = conf->wps;
	data->pwd_group = conf->pwd_group;
	if (conf->eap_req_id_text) {
		data->eap_req_id_text = os_malloc(conf->eap_req_id_text_len);
		if (data->eap_req_id_text) {
			os_memcpy(data->eap_req_id_text, conf->eap_req_id_text,
				  conf->eap_req_id_text_len);
			data->eap_req_id_text_len = conf->eap_req_id_text_len;
		}
	}

	data->clients = radius_server_read_clients(conf->client_file,
						   conf->ipv6);
	if (data->clients == NULL) {
		printf("No RADIUS clients configured.\n");
		radius_server_deinit(data);
		return NULL;
	}

#ifdef CONFIG_IPV6
	if (conf->ipv6)
		data->auth_sock = radius_server_open_socket6(conf->auth_port);
	else
#endif /* CONFIG_IPV6 */
	data->auth_sock = radius_server_open_socket(conf->auth_port);
	if (data->auth_sock < 0) {
		printf("Failed to open UDP socket for RADIUS authentication "
		       "server\n");
		radius_server_deinit(data);
		return NULL;
	}
	if (eloop_register_read_sock(data->auth_sock,
				     radius_server_receive_auth,
				     data, NULL)) {
		radius_server_deinit(data);
		return NULL;
	}

	return data;
}


/**
 * radius_server_deinit - Deinitialize RADIUS server
 * @data: RADIUS server context from radius_server_init()
 */
void radius_server_deinit(struct radius_server_data *data)
{
	if (data == NULL)
		return;

	if (data->auth_sock >= 0) {
		eloop_unregister_read_sock(data->auth_sock);
		close(data->auth_sock);
	}

	radius_server_free_clients(data, data->clients);

	os_free(data->pac_opaque_encr_key);
	os_free(data->eap_fast_a_id);
	os_free(data->eap_fast_a_id_info);
	os_free(data->eap_req_id_text);
	os_free(data);
}


/**
 * radius_server_get_mib - Get RADIUS server MIB information
 * @data: RADIUS server context from radius_server_init()
 * @buf: Buffer for returning the MIB data in text format
 * @buflen: buf length in octets
 * Returns: Number of octets written into buf
 */
int radius_server_get_mib(struct radius_server_data *data, char *buf,
			  size_t buflen)
{
	int ret, uptime;
	unsigned int idx;
	char *end, *pos;
	struct os_time now;
	struct radius_client *cli;

	/* RFC 2619 - RADIUS Authentication Server MIB */

	if (data == NULL || buflen == 0)
		return 0;

	pos = buf;
	end = buf + buflen;

	os_get_time(&now);
	uptime = (now.sec - data->start_time.sec) * 100 +
		((now.usec - data->start_time.usec) / 10000) % 100;
	ret = os_snprintf(pos, end - pos,
			  "RADIUS-AUTH-SERVER-MIB\n"
			  "radiusAuthServIdent=hostapd\n"
			  "radiusAuthServUpTime=%d\n"
			  "radiusAuthServResetTime=0\n"
			  "radiusAuthServConfigReset=4\n",
			  uptime);
	if (ret < 0 || ret >= end - pos) {
		*pos = '\0';
		return pos - buf;
	}
	pos += ret;

	ret = os_snprintf(pos, end - pos,
			  "radiusAuthServTotalAccessRequests=%u\n"
			  "radiusAuthServTotalInvalidRequests=%u\n"
			  "radiusAuthServTotalDupAccessRequests=%u\n"
			  "radiusAuthServTotalAccessAccepts=%u\n"
			  "radiusAuthServTotalAccessRejects=%u\n"
			  "radiusAuthServTotalAccessChallenges=%u\n"
			  "radiusAuthServTotalMalformedAccessRequests=%u\n"
			  "radiusAuthServTotalBadAuthenticators=%u\n"
			  "radiusAuthServTotalPacketsDropped=%u\n"
			  "radiusAuthServTotalUnknownTypes=%u\n",
			  data->counters.access_requests,
			  data->counters.invalid_requests,
			  data->counters.dup_access_requests,
			  data->counters.access_accepts,
			  data->counters.access_rejects,
			  data->counters.access_challenges,
			  data->counters.malformed_access_requests,
			  data->counters.bad_authenticators,
			  data->counters.packets_dropped,
			  data->counters.unknown_types);
	if (ret < 0 || ret >= end - pos) {
		*pos = '\0';
		return pos - buf;
	}
	pos += ret;

	for (cli = data->clients, idx = 0; cli; cli = cli->next, idx++) {
		char abuf[50], mbuf[50];
#ifdef CONFIG_IPV6
		if (data->ipv6) {
			if (inet_ntop(AF_INET6, &cli->addr6, abuf,
				      sizeof(abuf)) == NULL)
				abuf[0] = '\0';
			if (inet_ntop(AF_INET6, &cli->mask6, abuf,
				      sizeof(mbuf)) == NULL)
				mbuf[0] = '\0';
		}
#endif /* CONFIG_IPV6 */
		if (!data->ipv6) {
			os_strlcpy(abuf, inet_ntoa(cli->addr), sizeof(abuf));
			os_strlcpy(mbuf, inet_ntoa(cli->mask), sizeof(mbuf));
		}

		ret = os_snprintf(pos, end - pos,
				  "radiusAuthClientIndex=%u\n"
				  "radiusAuthClientAddress=%s/%s\n"
				  "radiusAuthServAccessRequests=%u\n"
				  "radiusAuthServDupAccessRequests=%u\n"
				  "radiusAuthServAccessAccepts=%u\n"
				  "radiusAuthServAccessRejects=%u\n"
				  "radiusAuthServAccessChallenges=%u\n"
				  "radiusAuthServMalformedAccessRequests=%u\n"
				  "radiusAuthServBadAuthenticators=%u\n"
				  "radiusAuthServPacketsDropped=%u\n"
				  "radiusAuthServUnknownTypes=%u\n",
				  idx,
				  abuf, mbuf,
				  cli->counters.access_requests,
				  cli->counters.dup_access_requests,
				  cli->counters.access_accepts,
				  cli->counters.access_rejects,
				  cli->counters.access_challenges,
				  cli->counters.malformed_access_requests,
				  cli->counters.bad_authenticators,
				  cli->counters.packets_dropped,
				  cli->counters.unknown_types);
		if (ret < 0 || ret >= end - pos) {
			*pos = '\0';
			return pos - buf;
		}
		pos += ret;
	}

	return pos - buf;
}


static int radius_server_get_eap_user(void *ctx, const u8 *identity,
				      size_t identity_len, int phase2,
				      struct eap_user *user)
{
	struct radius_session *sess = ctx;
	struct radius_server_data *data = sess->server;

	return data->get_eap_user(data->conf_ctx, identity, identity_len,
				  phase2, user);
}


static const char * radius_server_get_eap_req_id_text(void *ctx, size_t *len)
{
	struct radius_session *sess = ctx;
	struct radius_server_data *data = sess->server;
	*len = data->eap_req_id_text_len;
	return data->eap_req_id_text;
}


static struct eapol_callbacks radius_server_eapol_cb =
{
	.get_eap_user = radius_server_get_eap_user,
	.get_eap_req_id_text = radius_server_get_eap_req_id_text,
};


/**
 * radius_server_eap_pending_cb - Pending EAP data notification
 * @data: RADIUS server context from radius_server_init()
 * @ctx: Pending EAP context pointer
 *
 * This function is used to notify EAP server module that a pending operation
 * has been completed and processing of the EAP session can proceed.
 */
void radius_server_eap_pending_cb(struct radius_server_data *data, void *ctx)
{
	struct radius_client *cli;
	struct radius_session *s, *sess = NULL;
	struct radius_msg *msg;

	if (data == NULL)
		return;

	for (cli = data->clients; cli; cli = cli->next) {
		for (s = cli->sessions; s; s = s->next) {
			if (s->eap == ctx && s->last_msg) {
				sess = s;
				break;
			}
			if (sess)
				break;
		}
		if (sess)
			break;
	}

	if (sess == NULL) {
		RADIUS_DEBUG("No session matched callback ctx");
		return;
	}

	msg = sess->last_msg;
	sess->last_msg = NULL;
	eap_sm_pending_cb(sess->eap);
	if (radius_server_request(data, msg,
				  (struct sockaddr *) &sess->last_from,
				  sess->last_fromlen, cli,
				  sess->last_from_addr,
				  sess->last_from_port, sess) == -2)
		return; /* msg was stored with the session */

	radius_msg_free(msg);
}
