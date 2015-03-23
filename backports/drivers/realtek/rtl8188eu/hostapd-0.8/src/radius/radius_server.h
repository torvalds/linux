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

#ifndef RADIUS_SERVER_H
#define RADIUS_SERVER_H

struct radius_server_data;
struct eap_user;

/**
 * struct radius_server_conf - RADIUS server configuration
 */
struct radius_server_conf {
	/**
	 * auth_port - UDP port to listen to as an authentication server
	 */
	int auth_port;

	/**
	 * client_file - RADIUS client configuration file
	 *
	 * This file contains the RADIUS clients and the shared secret to be
	 * used with them in a format where each client is on its own line. The
	 * first item on the line is the IPv4 or IPv6 address of the client
	 * with an optional address mask to allow full network to be specified
	 * (e.g., 192.168.1.2 or 192.168.1.0/24). This is followed by white
	 * space (space or tabulator) and the shared secret. Lines starting
	 * with '#' are skipped and can be used as comments.
	 */
	char *client_file;

	/**
	 * conf_ctx - Context pointer for callbacks
	 *
	 * This is used as the ctx argument in get_eap_user() calls.
	 */
	void *conf_ctx;

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
	 * pwd_group - EAP-pwd D-H group
	 *
	 * This is used to select which D-H group to use with EAP-pwd.
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
	const char *eap_req_id_text;

	/**
	 * eap_req_id_text_len - Length of eap_req_id_text buffer in octets
	 */
	size_t eap_req_id_text_len;

	/*
	 * msg_ctx - Context data for wpa_msg() calls
	 */
	void *msg_ctx;
};


struct radius_server_data *
radius_server_init(struct radius_server_conf *conf);

void radius_server_deinit(struct radius_server_data *data);

int radius_server_get_mib(struct radius_server_data *data, char *buf,
			  size_t buflen);

void radius_server_eap_pending_cb(struct radius_server_data *data, void *ctx);

#endif /* RADIUS_SERVER_H */
