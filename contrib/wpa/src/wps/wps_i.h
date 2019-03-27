/*
 * Wi-Fi Protected Setup - internal definitions
 * Copyright (c) 2008-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPS_I_H
#define WPS_I_H

#include "wps.h"
#include "wps_attr_parse.h"

struct wps_nfc_pw_token;

/**
 * struct wps_data - WPS registration protocol data
 *
 * This data is stored at the EAP-WSC server/peer method and it is kept for a
 * single registration protocol run.
 */
struct wps_data {
	/**
	 * wps - Pointer to long term WPS context
	 */
	struct wps_context *wps;

	/**
	 * registrar - Whether this end is a Registrar
	 */
	int registrar;

	/**
	 * er - Whether the local end is an external registrar
	 */
	int er;

	enum {
		/* Enrollee states */
		SEND_M1, RECV_M2, SEND_M3, RECV_M4, SEND_M5, RECV_M6, SEND_M7,
		RECV_M8, RECEIVED_M2D, WPS_MSG_DONE, RECV_ACK, WPS_FINISHED,
		SEND_WSC_NACK,

		/* Registrar states */
		RECV_M1, SEND_M2, RECV_M3, SEND_M4, RECV_M5, SEND_M6,
		RECV_M7, SEND_M8, RECV_DONE, SEND_M2D, RECV_M2D_ACK
	} state;

	u8 uuid_e[WPS_UUID_LEN];
	u8 uuid_r[WPS_UUID_LEN];
	u8 mac_addr_e[ETH_ALEN];
	u8 nonce_e[WPS_NONCE_LEN];
	u8 nonce_r[WPS_NONCE_LEN];
	u8 psk1[WPS_PSK_LEN];
	u8 psk2[WPS_PSK_LEN];
	u8 snonce[2 * WPS_SECRET_NONCE_LEN];
	u8 peer_hash1[WPS_HASH_LEN];
	u8 peer_hash2[WPS_HASH_LEN];

	struct wpabuf *dh_privkey;
	struct wpabuf *dh_pubkey_e;
	struct wpabuf *dh_pubkey_r;
	u8 authkey[WPS_AUTHKEY_LEN];
	u8 keywrapkey[WPS_KEYWRAPKEY_LEN];
	u8 emsk[WPS_EMSK_LEN];

	struct wpabuf *last_msg;

	u8 *dev_password;
	size_t dev_password_len;
	u16 dev_pw_id;
	int pbc;
	u8 *alt_dev_password;
	size_t alt_dev_password_len;
	u16 alt_dev_pw_id;

	u8 peer_pubkey_hash[WPS_OOB_PUBKEY_HASH_LEN];
	int peer_pubkey_hash_set;

	/**
	 * request_type - Request Type attribute from (Re)AssocReq
	 */
	u8 request_type;

	/**
	 * encr_type - Available encryption types
	 */
	u16 encr_type;

	/**
	 * auth_type - Available authentication types
	 */
	u16 auth_type;

	u8 *new_psk;
	size_t new_psk_len;

	int wps_pin_revealed;
	struct wps_credential cred;

	struct wps_device_data peer_dev;

	/**
	 * config_error - Configuration Error value to be used in NACK
	 */
	u16 config_error;
	u16 error_indication;

	int ext_reg;
	int int_reg;

	struct wps_credential *new_ap_settings;

	void *dh_ctx;

	void (*ap_settings_cb)(void *ctx, const struct wps_credential *cred);
	void *ap_settings_cb_ctx;

	struct wps_credential *use_cred;

	int use_psk_key;
	u8 p2p_dev_addr[ETH_ALEN]; /* P2P Device Address of the client or
				    * 00:00:00:00:00:00 if not a P2p client */
	int pbc_in_m1;

	struct wps_nfc_pw_token *nfc_pw_token;
};


/* wps_common.c */
void wps_kdf(const u8 *key, const u8 *label_prefix, size_t label_prefix_len,
	     const char *label, u8 *res, size_t res_len);
int wps_derive_keys(struct wps_data *wps);
int wps_derive_psk(struct wps_data *wps, const u8 *dev_passwd,
		   size_t dev_passwd_len);
struct wpabuf * wps_decrypt_encr_settings(struct wps_data *wps, const u8 *encr,
					  size_t encr_len);
void wps_fail_event(struct wps_context *wps, enum wps_msg_type msg,
		    u16 config_error, u16 error_indication, const u8 *mac_addr);
void wps_success_event(struct wps_context *wps, const u8 *mac_addr);
void wps_pwd_auth_fail_event(struct wps_context *wps, int enrollee, int part,
			     const u8 *mac_addr);
void wps_pbc_overlap_event(struct wps_context *wps);
void wps_pbc_timeout_event(struct wps_context *wps);
void wps_pbc_active_event(struct wps_context *wps);
void wps_pbc_disable_event(struct wps_context *wps);

struct wpabuf * wps_build_wsc_ack(struct wps_data *wps);
struct wpabuf * wps_build_wsc_nack(struct wps_data *wps);

/* wps_attr_build.c */
int wps_build_public_key(struct wps_data *wps, struct wpabuf *msg);
int wps_build_req_type(struct wpabuf *msg, enum wps_request_type type);
int wps_build_resp_type(struct wpabuf *msg, enum wps_response_type type);
int wps_build_config_methods(struct wpabuf *msg, u16 methods);
int wps_build_uuid_e(struct wpabuf *msg, const u8 *uuid);
int wps_build_dev_password_id(struct wpabuf *msg, u16 id);
int wps_build_config_error(struct wpabuf *msg, u16 err);
int wps_build_authenticator(struct wps_data *wps, struct wpabuf *msg);
int wps_build_key_wrap_auth(struct wps_data *wps, struct wpabuf *msg);
int wps_build_encr_settings(struct wps_data *wps, struct wpabuf *msg,
			    struct wpabuf *plain);
int wps_build_version(struct wpabuf *msg);
int wps_build_wfa_ext(struct wpabuf *msg, int req_to_enroll,
		      const u8 *auth_macs, size_t auth_macs_count);
int wps_build_msg_type(struct wpabuf *msg, enum wps_msg_type msg_type);
int wps_build_enrollee_nonce(struct wps_data *wps, struct wpabuf *msg);
int wps_build_registrar_nonce(struct wps_data *wps, struct wpabuf *msg);
int wps_build_auth_type_flags(struct wps_data *wps, struct wpabuf *msg);
int wps_build_encr_type_flags(struct wps_data *wps, struct wpabuf *msg);
int wps_build_conn_type_flags(struct wps_data *wps, struct wpabuf *msg);
int wps_build_assoc_state(struct wps_data *wps, struct wpabuf *msg);
int wps_build_oob_dev_pw(struct wpabuf *msg, u16 dev_pw_id,
			 const struct wpabuf *pubkey, const u8 *dev_pw,
			 size_t dev_pw_len);
struct wpabuf * wps_ie_encapsulate(struct wpabuf *data);
int wps_build_mac_addr(struct wpabuf *msg, const u8 *addr);
int wps_build_rf_bands_attr(struct wpabuf *msg, u8 rf_bands);
int wps_build_ap_channel(struct wpabuf *msg, u16 ap_channel);

/* wps_attr_process.c */
int wps_process_authenticator(struct wps_data *wps, const u8 *authenticator,
			      const struct wpabuf *msg);
int wps_process_key_wrap_auth(struct wps_data *wps, struct wpabuf *msg,
			      const u8 *key_wrap_auth);
int wps_process_cred(struct wps_parse_attr *attr,
		     struct wps_credential *cred);
int wps_process_ap_settings(struct wps_parse_attr *attr,
			    struct wps_credential *cred);

/* wps_enrollee.c */
struct wpabuf * wps_enrollee_get_msg(struct wps_data *wps,
				     enum wsc_op_code *op_code);
enum wps_process_res wps_enrollee_process_msg(struct wps_data *wps,
					      enum wsc_op_code op_code,
					      const struct wpabuf *msg);

/* wps_registrar.c */
struct wpabuf * wps_registrar_get_msg(struct wps_data *wps,
				      enum wsc_op_code *op_code);
enum wps_process_res wps_registrar_process_msg(struct wps_data *wps,
					       enum wsc_op_code op_code,
					       const struct wpabuf *msg);
int wps_build_cred(struct wps_data *wps, struct wpabuf *msg);
int wps_device_store(struct wps_registrar *reg,
		     struct wps_device_data *dev, const u8 *uuid);
void wps_registrar_selected_registrar_changed(struct wps_registrar *reg,
					      u16 dev_pw_id);
const u8 * wps_authorized_macs(struct wps_registrar *reg, size_t *count);
int wps_registrar_pbc_overlap(struct wps_registrar *reg,
			      const u8 *addr, const u8 *uuid_e);
void wps_registrar_remove_nfc_pw_token(struct wps_registrar *reg,
				       struct wps_nfc_pw_token *token);
int wps_cb_new_psk(struct wps_registrar *reg, const u8 *mac_addr,
		   const u8 *p2p_dev_addr, const u8 *psk, size_t psk_len);

#endif /* WPS_I_H */
