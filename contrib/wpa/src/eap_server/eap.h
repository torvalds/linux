/*
 * hostapd / EAP Full Authenticator state machine (RFC 4137)
 * Copyright (c) 2004-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAP_H
#define EAP_H

#include "common/defs.h"
#include "utils/list.h"
#include "eap_common/eap_defs.h"
#include "eap_server/eap_methods.h"
#include "wpabuf.h"

struct eap_sm;

#define EAP_TTLS_AUTH_PAP 1
#define EAP_TTLS_AUTH_CHAP 2
#define EAP_TTLS_AUTH_MSCHAP 4
#define EAP_TTLS_AUTH_MSCHAPV2 8

struct eap_user {
	struct {
		int vendor;
		u32 method;
	} methods[EAP_MAX_METHODS];
	u8 *password;
	size_t password_len;
	int password_hash; /* whether password is hashed with
			    * nt_password_hash() */
	u8 *salt;
	size_t salt_len;
	int phase2;
	int force_version;
	unsigned int remediation:1;
	unsigned int macacl:1;
	int ttls_auth; /* bitfield of
			* EAP_TTLS_AUTH_{PAP,CHAP,MSCHAP,MSCHAPV2} */
	struct hostapd_radius_attr *accept_attr;
	u32 t_c_timestamp;
};

struct eap_eapol_interface {
	/* Lower layer to full authenticator variables */
	Boolean eapResp; /* shared with EAPOL Backend Authentication */
	struct wpabuf *eapRespData;
	Boolean portEnabled;
	int retransWhile;
	Boolean eapRestart; /* shared with EAPOL Authenticator PAE */
	int eapSRTT;
	int eapRTTVAR;

	/* Full authenticator to lower layer variables */
	Boolean eapReq; /* shared with EAPOL Backend Authentication */
	Boolean eapNoReq; /* shared with EAPOL Backend Authentication */
	Boolean eapSuccess;
	Boolean eapFail;
	Boolean eapTimeout;
	struct wpabuf *eapReqData;
	u8 *eapKeyData;
	size_t eapKeyDataLen;
	u8 *eapSessionId;
	size_t eapSessionIdLen;
	Boolean eapKeyAvailable; /* called keyAvailable in IEEE 802.1X-2004 */

	/* AAA interface to full authenticator variables */
	Boolean aaaEapReq;
	Boolean aaaEapNoReq;
	Boolean aaaSuccess;
	Boolean aaaFail;
	struct wpabuf *aaaEapReqData;
	u8 *aaaEapKeyData;
	size_t aaaEapKeyDataLen;
	Boolean aaaEapKeyAvailable;
	int aaaMethodTimeout;

	/* Full authenticator to AAA interface variables */
	Boolean aaaEapResp;
	struct wpabuf *aaaEapRespData;
	/* aaaIdentity -> eap_get_identity() */
	Boolean aaaTimeout;
};

struct eap_server_erp_key {
	struct dl_list list;
	size_t rRK_len;
	size_t rIK_len;
	u8 rRK[ERP_MAX_KEY_LEN];
	u8 rIK[ERP_MAX_KEY_LEN];
	u32 recv_seq;
	u8 cryptosuite;
	char keyname_nai[];
};

struct eapol_callbacks {
	int (*get_eap_user)(void *ctx, const u8 *identity, size_t identity_len,
			    int phase2, struct eap_user *user);
	const char * (*get_eap_req_id_text)(void *ctx, size_t *len);
	void (*log_msg)(void *ctx, const char *msg);
	int (*get_erp_send_reauth_start)(void *ctx);
	const char * (*get_erp_domain)(void *ctx);
	struct eap_server_erp_key * (*erp_get_key)(void *ctx,
						   const char *keyname);
	int (*erp_add_key)(void *ctx, struct eap_server_erp_key *erp);
};

struct eap_config {
	void *ssl_ctx;
	void *msg_ctx;
	void *eap_sim_db_priv;
	Boolean backend_auth;
	int eap_server;
	u16 pwd_group;
	u8 *pac_opaque_encr_key;
	u8 *eap_fast_a_id;
	size_t eap_fast_a_id_len;
	char *eap_fast_a_id_info;
	int eap_fast_prov;
	int pac_key_lifetime;
	int pac_key_refresh_time;
	int eap_sim_aka_result_ind;
	int tnc;
	struct wps_context *wps;
	const struct wpabuf *assoc_wps_ie;
	const struct wpabuf *assoc_p2p_ie;
	const u8 *peer_addr;
	int fragment_size;

	int pbc_in_m1;

	const u8 *server_id;
	size_t server_id_len;
	int erp;
	unsigned int tls_session_lifetime;
	unsigned int tls_flags;

#ifdef CONFIG_TESTING_OPTIONS
	u32 tls_test_flags;
#endif /* CONFIG_TESTING_OPTIONS */
};


struct eap_sm * eap_server_sm_init(void *eapol_ctx,
				   const struct eapol_callbacks *eapol_cb,
				   struct eap_config *eap_conf);
void eap_server_sm_deinit(struct eap_sm *sm);
int eap_server_sm_step(struct eap_sm *sm);
void eap_sm_notify_cached(struct eap_sm *sm);
void eap_sm_pending_cb(struct eap_sm *sm);
int eap_sm_method_pending(struct eap_sm *sm);
const u8 * eap_get_identity(struct eap_sm *sm, size_t *len);
const char * eap_get_serial_num(struct eap_sm *sm);
struct eap_eapol_interface * eap_get_interface(struct eap_sm *sm);
void eap_server_clear_identity(struct eap_sm *sm);
void eap_server_mschap_rx_callback(struct eap_sm *sm, const char *source,
				   const u8 *username, size_t username_len,
				   const u8 *challenge, const u8 *response);
void eap_erp_update_identity(struct eap_sm *sm, const u8 *eap, size_t len);

#endif /* EAP_H */
