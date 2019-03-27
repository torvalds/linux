/*
 * EAP peer state machine functions (RFC 4137)
 * Copyright (c) 2004-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAP_H
#define EAP_H

#include "common/defs.h"
#include "eap_common/eap_defs.h"
#include "eap_peer/eap_methods.h"

struct eap_sm;
struct wpa_config_blob;
struct wpabuf;

struct eap_method_type {
	int vendor;
	u32 method;
};

#ifdef IEEE8021X_EAPOL

/**
 * enum eapol_bool_var - EAPOL boolean state variables for EAP state machine
 *
 * These variables are used in the interface between EAP peer state machine and
 * lower layer. These are defined in RFC 4137, Sect. 4.1. Lower layer code is
 * expected to maintain these variables and register a callback functions for
 * EAP state machine to get and set the variables.
 */
enum eapol_bool_var {
	/**
	 * EAPOL_eapSuccess - EAP SUCCESS state reached
	 *
	 * EAP state machine reads and writes this value.
	 */
	EAPOL_eapSuccess,

	/**
	 * EAPOL_eapRestart - Lower layer request to restart authentication
	 *
	 * Set to TRUE in lower layer, FALSE in EAP state machine.
	 */
	EAPOL_eapRestart,

	/**
	 * EAPOL_eapFail - EAP FAILURE state reached
	 *
	 * EAP state machine writes this value.
	 */
	EAPOL_eapFail,

	/**
	 * EAPOL_eapResp - Response to send
	 *
	 * Set to TRUE in EAP state machine, FALSE in lower layer.
	 */
	EAPOL_eapResp,

	/**
	 * EAPOL_eapNoResp - Request has been process; no response to send
	 *
	 * Set to TRUE in EAP state machine, FALSE in lower layer.
	 */
	EAPOL_eapNoResp,

	/**
	 * EAPOL_eapReq - EAP request available from lower layer
	 *
	 * Set to TRUE in lower layer, FALSE in EAP state machine.
	 */
	EAPOL_eapReq,

	/**
	 * EAPOL_portEnabled - Lower layer is ready for communication
	 *
	 * EAP state machines reads this value.
	 */
	EAPOL_portEnabled,

	/**
	 * EAPOL_altAccept - Alternate indication of success (RFC3748)
	 *
	 * EAP state machines reads this value.
	 */
	EAPOL_altAccept,

	/**
	 * EAPOL_altReject - Alternate indication of failure (RFC3748)
	 *
	 * EAP state machines reads this value.
	 */
	EAPOL_altReject,

	/**
	 * EAPOL_eapTriggerStart - EAP-based trigger to send EAPOL-Start
	 *
	 * EAP state machine writes this value.
	 */
	EAPOL_eapTriggerStart
};

/**
 * enum eapol_int_var - EAPOL integer state variables for EAP state machine
 *
 * These variables are used in the interface between EAP peer state machine and
 * lower layer. These are defined in RFC 4137, Sect. 4.1. Lower layer code is
 * expected to maintain these variables and register a callback functions for
 * EAP state machine to get and set the variables.
 */
enum eapol_int_var {
	/**
	 * EAPOL_idleWhile - Outside time for EAP peer timeout
	 *
	 * This integer variable is used to provide an outside timer that the
	 * external (to EAP state machine) code must decrement by one every
	 * second until the value reaches zero. This is used in the same way as
	 * EAPOL state machine timers. EAP state machine reads and writes this
	 * value.
	 */
	EAPOL_idleWhile
};

/**
 * struct eapol_callbacks - Callback functions from EAP to lower layer
 *
 * This structure defines the callback functions that EAP state machine
 * requires from the lower layer (usually EAPOL state machine) for updating
 * state variables and requesting information. eapol_ctx from
 * eap_peer_sm_init() call will be used as the ctx parameter for these
 * callback functions.
 */
struct eapol_callbacks {
	/**
	 * get_config - Get pointer to the current network configuration
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 */
	struct eap_peer_config * (*get_config)(void *ctx);

	/**
	 * get_bool - Get a boolean EAPOL state variable
	 * @variable: EAPOL boolean variable to get
	 * Returns: Value of the EAPOL variable
	 */
	Boolean (*get_bool)(void *ctx, enum eapol_bool_var variable);

	/**
	 * set_bool - Set a boolean EAPOL state variable
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 * @variable: EAPOL boolean variable to set
	 * @value: Value for the EAPOL variable
	 */
	void (*set_bool)(void *ctx, enum eapol_bool_var variable,
			 Boolean value);

	/**
	 * get_int - Get an integer EAPOL state variable
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 * @variable: EAPOL integer variable to get
	 * Returns: Value of the EAPOL variable
	 */
	unsigned int (*get_int)(void *ctx, enum eapol_int_var variable);

	/**
	 * set_int - Set an integer EAPOL state variable
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 * @variable: EAPOL integer variable to set
	 * @value: Value for the EAPOL variable
	 */
	void (*set_int)(void *ctx, enum eapol_int_var variable,
			unsigned int value);

	/**
	 * get_eapReqData - Get EAP-Request data
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 * @len: Pointer to variable that will be set to eapReqDataLen
	 * Returns: Reference to eapReqData (EAP state machine will not free
	 * this) or %NULL if eapReqData not available.
	 */
	struct wpabuf * (*get_eapReqData)(void *ctx);

	/**
	 * set_config_blob - Set named configuration blob
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 * @blob: New value for the blob
	 *
	 * Adds a new configuration blob or replaces the current value of an
	 * existing blob.
	 */
	void (*set_config_blob)(void *ctx, struct wpa_config_blob *blob);

	/**
	 * get_config_blob - Get a named configuration blob
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 * @name: Name of the blob
	 * Returns: Pointer to blob data or %NULL if not found
	 */
	const struct wpa_config_blob * (*get_config_blob)(void *ctx,
							  const char *name);

	/**
	 * notify_pending - Notify that a pending request can be retried
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 *
	 * An EAP method can perform a pending operation (e.g., to get a
	 * response from an external process). Once the response is available,
	 * this callback function can be used to request EAPOL state machine to
	 * retry delivering the previously received (and still unanswered) EAP
	 * request to EAP state machine.
	 */
	void (*notify_pending)(void *ctx);

	/**
	 * eap_param_needed - Notify that EAP parameter is needed
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 * @field: Field indicator (e.g., WPA_CTRL_REQ_EAP_IDENTITY)
	 * @txt: User readable text describing the required parameter
	 */
	void (*eap_param_needed)(void *ctx, enum wpa_ctrl_req_type field,
				 const char *txt);

	/**
	 * notify_cert - Notification of a peer certificate
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 * @depth: Depth in certificate chain (0 = server)
	 * @subject: Subject of the peer certificate
	 * @altsubject: Select fields from AltSubject of the peer certificate
	 * @num_altsubject: Number of altsubject values
	 * @cert_hash: SHA-256 hash of the certificate
	 * @cert: Peer certificate
	 */
	void (*notify_cert)(void *ctx, int depth, const char *subject,
			    const char *altsubject[], int num_altsubject,
			    const char *cert_hash, const struct wpabuf *cert);

	/**
	 * notify_status - Notification of the current EAP state
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 * @status: Step in the process of EAP authentication
	 * @parameter: Step-specific parameter, e.g., EAP method name
	 */
	void (*notify_status)(void *ctx, const char *status,
			      const char *parameter);

	/**
	 * notify_eap_error - Report EAP method error code
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 * @error_code: Error code from the used EAP method
	 */
	void (*notify_eap_error)(void *ctx, int error_code);

#ifdef CONFIG_EAP_PROXY
	/**
	 * eap_proxy_cb - Callback signifying any updates from eap_proxy
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 */
	void (*eap_proxy_cb)(void *ctx);

	/**
	 * eap_proxy_notify_sim_status - Notification of SIM status change
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 * @sim_state: One of enum value from sim_state
	 */
	void (*eap_proxy_notify_sim_status)(void *ctx,
					    enum eap_proxy_sim_state sim_state);

	/**
	 * get_imsi - Get the IMSI value from eap_proxy
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 * @sim_num: SIM/USIM number to get the IMSI value for
	 * @imsi: Buffer for IMSI value
	 * @len: Buffer for returning IMSI length in octets
	 * Returns: MNC length (2 or 3) or -1 on error
	 */
	int (*get_imsi)(void *ctx, int sim_num, char *imsi, size_t *len);
#endif /* CONFIG_EAP_PROXY */

	/**
	 * set_anon_id - Set or add anonymous identity
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 * @id: Anonymous identity (e.g., EAP-SIM pseudonym) or %NULL to clear
	 * @len: Length of anonymous identity in octets
	 */
	void (*set_anon_id)(void *ctx, const u8 *id, size_t len);
};

/**
 * struct eap_config - Configuration for EAP state machine
 */
struct eap_config {
	/**
	 * opensc_engine_path - OpenSC engine for OpenSSL engine support
	 *
	 * Usually, path to engine_opensc.so.
	 */
	const char *opensc_engine_path;
	/**
	 * pkcs11_engine_path - PKCS#11 engine for OpenSSL engine support
	 *
	 * Usually, path to engine_pkcs11.so.
	 */
	const char *pkcs11_engine_path;
	/**
	 * pkcs11_module_path - OpenSC PKCS#11 module for OpenSSL engine
	 *
	 * Usually, path to opensc-pkcs11.so.
	 */
	const char *pkcs11_module_path;
	/**
	 * openssl_ciphers - OpenSSL cipher string
	 *
	 * This is an OpenSSL specific configuration option for configuring the
	 * default ciphers. If not set, "DEFAULT:!EXP:!LOW" is used as the
	 * default.
	 */
	const char *openssl_ciphers;
	/**
	 * wps - WPS context data
	 *
	 * This is only used by EAP-WSC and can be left %NULL if not available.
	 */
	struct wps_context *wps;

	/**
	 * cert_in_cb - Include server certificates in callback
	 */
	int cert_in_cb;
};

struct eap_sm * eap_peer_sm_init(void *eapol_ctx,
				 const struct eapol_callbacks *eapol_cb,
				 void *msg_ctx, struct eap_config *conf);
void eap_peer_sm_deinit(struct eap_sm *sm);
int eap_peer_sm_step(struct eap_sm *sm);
void eap_sm_abort(struct eap_sm *sm);
int eap_sm_get_status(struct eap_sm *sm, char *buf, size_t buflen,
		      int verbose);
const char * eap_sm_get_method_name(struct eap_sm *sm);
struct wpabuf * eap_sm_buildIdentity(struct eap_sm *sm, int id, int encrypted);
void eap_sm_request_identity(struct eap_sm *sm);
void eap_sm_request_password(struct eap_sm *sm);
void eap_sm_request_new_password(struct eap_sm *sm);
void eap_sm_request_pin(struct eap_sm *sm);
void eap_sm_request_otp(struct eap_sm *sm, const char *msg, size_t msg_len);
void eap_sm_request_passphrase(struct eap_sm *sm);
void eap_sm_request_sim(struct eap_sm *sm, const char *req);
void eap_sm_notify_ctrl_attached(struct eap_sm *sm);
u32 eap_get_phase2_type(const char *name, int *vendor);
struct eap_method_type * eap_get_phase2_types(struct eap_peer_config *config,
					      size_t *count);
void eap_set_fast_reauth(struct eap_sm *sm, int enabled);
void eap_set_workaround(struct eap_sm *sm, unsigned int workaround);
void eap_set_force_disabled(struct eap_sm *sm, int disabled);
void eap_set_external_sim(struct eap_sm *sm, int external_sim);
int eap_key_available(struct eap_sm *sm);
void eap_notify_success(struct eap_sm *sm);
void eap_notify_lower_layer_success(struct eap_sm *sm);
const u8 * eap_get_eapSessionId(struct eap_sm *sm, size_t *len);
const u8 * eap_get_eapKeyData(struct eap_sm *sm, size_t *len);
struct wpabuf * eap_get_eapRespData(struct eap_sm *sm);
void eap_register_scard_ctx(struct eap_sm *sm, void *ctx);
void eap_invalidate_cached_session(struct eap_sm *sm);

int eap_is_wps_pbc_enrollee(struct eap_peer_config *conf);
int eap_is_wps_pin_enrollee(struct eap_peer_config *conf);

struct ext_password_data;
void eap_sm_set_ext_pw_ctx(struct eap_sm *sm, struct ext_password_data *ext);
void eap_set_anon_id(struct eap_sm *sm, const u8 *id, size_t len);
int eap_peer_was_failure_expected(struct eap_sm *sm);
void eap_peer_erp_free_keys(struct eap_sm *sm);
struct wpabuf * eap_peer_build_erp_reauth_start(struct eap_sm *sm, u8 eap_id);
void eap_peer_finish(struct eap_sm *sm, const struct eap_hdr *hdr, size_t len);
int eap_peer_get_erp_info(struct eap_sm *sm, struct eap_peer_config *config,
			  const u8 **username, size_t *username_len,
			  const u8 **realm, size_t *realm_len, u16 *erp_seq_num,
			  const u8 **rrk, size_t *rrk_len);
int eap_peer_update_erp_next_seq_num(struct eap_sm *sm, u16 seq_num);
void eap_peer_erp_init(struct eap_sm *sm, u8 *ext_session_id,
		       size_t ext_session_id_len, u8 *ext_emsk,
		       size_t ext_emsk_len);

#endif /* IEEE8021X_EAPOL */

#endif /* EAP_H */
