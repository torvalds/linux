/*
 * EAPOL supplicant state machines
 * Copyright (c) 2004-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAPOL_SUPP_SM_H
#define EAPOL_SUPP_SM_H

#include "common/defs.h"

typedef enum { Unauthorized, Authorized } PortStatus;
typedef enum { Auto, ForceUnauthorized, ForceAuthorized } PortControl;

/**
 * struct eapol_config - Per network configuration for EAPOL state machines
 */
struct eapol_config {
	/**
	 * accept_802_1x_keys - Accept IEEE 802.1X (non-WPA) EAPOL-Key frames
	 *
	 * This variable should be set to 1 when using EAPOL state machines
	 * with non-WPA security policy to generate dynamic WEP keys. When
	 * using WPA, this should be set to 0 so that WPA state machine can
	 * process the EAPOL-Key frames.
	 */
	int accept_802_1x_keys;

#define EAPOL_REQUIRE_KEY_UNICAST BIT(0)
#define EAPOL_REQUIRE_KEY_BROADCAST BIT(1)
	/**
	 * required_keys - Which EAPOL-Key packets are required
	 *
	 * This variable determines which EAPOL-Key packets are required before
	 * marking connection authenticated. This is a bit field of
	 * EAPOL_REQUIRE_KEY_UNICAST and EAPOL_REQUIRE_KEY_BROADCAST flags.
	 */
	int required_keys;

	/**
	 * fast_reauth - Whether fast EAP reauthentication is enabled
	 */
	int fast_reauth;

	/**
	 * workaround - Whether EAP workarounds are enabled
	 */
	unsigned int workaround;

	/**
	 * eap_disabled - Whether EAP is disabled
	 */
	int eap_disabled;

	/**
	 * external_sim - Use external processing for SIM/USIM operations
	 */
	int external_sim;

#define EAPOL_LOCAL_WPS_IN_USE BIT(0)
#define EAPOL_PEER_IS_WPS20_AP BIT(1)
	/**
	 * wps - Whether this connection is used for WPS
	 */
	int wps;
};

struct eapol_sm;
struct wpa_config_blob;

enum eapol_supp_result {
	EAPOL_SUPP_RESULT_FAILURE,
	EAPOL_SUPP_RESULT_SUCCESS,
	EAPOL_SUPP_RESULT_EXPECTED_FAILURE
};

/**
 * struct eapol_ctx - Global (for all networks) EAPOL state machine context
 */
struct eapol_ctx {
	/**
	 * ctx - Pointer to arbitrary upper level context
	 */
	void *ctx;

	/**
	 * preauth - IEEE 802.11i/RSN pre-authentication
	 *
	 * This EAPOL state machine is used for IEEE 802.11i/RSN
	 * pre-authentication
	 */
	int preauth;

	/**
	 * cb - Function to be called when EAPOL negotiation has been completed
	 * @eapol: Pointer to EAPOL state machine data
	 * @result: Whether the authentication was completed successfully
	 * @ctx: Pointer to context data (cb_ctx)
	 *
	 * This optional callback function will be called when the EAPOL
	 * authentication has been completed. This allows the owner of the
	 * EAPOL state machine to process the key and terminate the EAPOL state
	 * machine. Currently, this is used only in RSN pre-authentication.
	 */
	void (*cb)(struct eapol_sm *eapol, enum eapol_supp_result result,
		   void *ctx);

	/**
	 * cb_ctx - Callback context for cb()
	 */
	void *cb_ctx;

	/**
	 * msg_ctx - Callback context for wpa_msg() calls
	 */
	void *msg_ctx;

	/**
	 * scard_ctx - Callback context for PC/SC scard_*() function calls
	 *
	 * This context can be updated with eapol_sm_register_scard_ctx().
	 */
	void *scard_ctx;

	/**
	 * eapol_send_ctx - Callback context for eapol_send() calls
	 */
	void *eapol_send_ctx;

	/**
	 * eapol_done_cb - Function to be called at successful completion
	 * @ctx: Callback context (ctx)
	 *
	 * This function is called at the successful completion of EAPOL
	 * authentication. If dynamic WEP keys are used, this is called only
	 * after all the expected keys have been received.
	 */
	void (*eapol_done_cb)(void *ctx);

	/**
	 * eapol_send - Send EAPOL packets
	 * @ctx: Callback context (eapol_send_ctx)
	 * @type: EAPOL type (IEEE802_1X_TYPE_*)
	 * @buf: Pointer to EAPOL payload
	 * @len: Length of the EAPOL payload
	 * Returns: 0 on success, -1 on failure
	 */
	int (*eapol_send)(void *ctx, int type, const u8 *buf, size_t len);

	/**
	 * set_wep_key - Configure WEP keys
	 * @ctx: Callback context (ctx)
	 * @unicast: Non-zero = unicast, 0 = multicast/broadcast key
	 * @keyidx: Key index (0..3)
	 * @key: WEP key
	 * @keylen: Length of the WEP key
	 * Returns: 0 on success, -1 on failure
	 */
	int (*set_wep_key)(void *ctx, int unicast, int keyidx,
			   const u8 *key, size_t keylen);

	/**
	 * set_config_blob - Set or add a named configuration blob
	 * @ctx: Callback context (ctx)
	 * @blob: New value for the blob
	 *
	 * Adds a new configuration blob or replaces the current value of an
	 * existing blob.
	 */
	void (*set_config_blob)(void *ctx, struct wpa_config_blob *blob);

	/**
	 * get_config_blob - Get a named configuration blob
	 * @ctx: Callback context (ctx)
	 * @name: Name of the blob
	 * Returns: Pointer to blob data or %NULL if not found
	 */
	const struct wpa_config_blob * (*get_config_blob)(void *ctx,
							  const char *name);

	/**
	 * aborted_cached - Notify that cached PMK attempt was aborted
	 * @ctx: Callback context (ctx)
	 */
	void (*aborted_cached)(void *ctx);

	/**
	 * opensc_engine_path - Path to the OpenSSL engine for opensc
	 *
	 * This is an OpenSSL specific configuration option for loading OpenSC
	 * engine (engine_opensc.so); if %NULL, this engine is not loaded.
	 */
	const char *opensc_engine_path;

	/**
	 * pkcs11_engine_path - Path to the OpenSSL engine for PKCS#11
	 *
	 * This is an OpenSSL specific configuration option for loading PKCS#11
	 * engine (engine_pkcs11.so); if %NULL, this engine is not loaded.
	 */
	const char *pkcs11_engine_path;

	/**
	 * pkcs11_module_path - Path to the OpenSSL OpenSC/PKCS#11 module
	 *
	 * This is an OpenSSL specific configuration option for configuring
	 * path to OpenSC/PKCS#11 engine (opensc-pkcs11.so); if %NULL, this
	 * module is not loaded.
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
	 * eap_param_needed - Notify that EAP parameter is needed
	 * @ctx: Callback context (ctx)
	 * @field: Field indicator (e.g., WPA_CTRL_REQ_EAP_IDENTITY)
	 * @txt: User readable text describing the required parameter
	 */
	void (*eap_param_needed)(void *ctx, enum wpa_ctrl_req_type field,
				 const char *txt);

	/**
	 * port_cb - Set port authorized/unauthorized callback (optional)
	 * @ctx: Callback context (ctx)
	 * @authorized: Whether the supplicant port is now in authorized state
	 */
	void (*port_cb)(void *ctx, int authorized);

	/**
	 * cert_cb - Notification of a peer certificate
	 * @ctx: Callback context (ctx)
	 * @depth: Depth in certificate chain (0 = server)
	 * @subject: Subject of the peer certificate
	 * @altsubject: Select fields from AltSubject of the peer certificate
	 * @num_altsubject: Number of altsubject values
	 * @cert_hash: SHA-256 hash of the certificate
	 * @cert: Peer certificate
	 */
	void (*cert_cb)(void *ctx, int depth, const char *subject,
			const char *altsubject[], int num_altsubject,
			const char *cert_hash, const struct wpabuf *cert);

	/**
	 * cert_in_cb - Include server certificates in callback
	 */
	int cert_in_cb;

	/**
	 * status_cb - Notification of a change in EAP status
	 * @ctx: Callback context (ctx)
	 * @status: Step in the process of EAP authentication
	 * @parameter: Step-specific parameter, e.g., EAP method name
	 */
	void (*status_cb)(void *ctx, const char *status,
			  const char *parameter);

	/**
	 * eap_error_cb - Notification of EAP method error
	 * @ctx: Callback context (ctx)
	 * @error_code: EAP method error code
	 */
	void (*eap_error_cb)(void *ctx, int error_code);

#ifdef CONFIG_EAP_PROXY
	/**
	 * eap_proxy_cb - Callback signifying any updates from eap_proxy
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 */
	void (*eap_proxy_cb)(void *ctx);

	/**
	 * eap_proxy_notify_sim_status - Notification of SIM status change
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 * @status: One of enum value from sim_state
	 */
	void (*eap_proxy_notify_sim_status)(void *ctx,
					    enum eap_proxy_sim_state sim_state);
#endif /* CONFIG_EAP_PROXY */

	/**
	 * set_anon_id - Set or add anonymous identity
	 * @ctx: eapol_ctx from eap_peer_sm_init() call
	 * @id: Anonymous identity (e.g., EAP-SIM pseudonym)
	 * @len: Length of anonymous identity in octets
	 */
	void (*set_anon_id)(void *ctx, const u8 *id, size_t len);
};


struct eap_peer_config;
struct ext_password_data;

#ifdef IEEE8021X_EAPOL
struct eapol_sm *eapol_sm_init(struct eapol_ctx *ctx);
void eapol_sm_deinit(struct eapol_sm *sm);
void eapol_sm_step(struct eapol_sm *sm);
int eapol_sm_get_status(struct eapol_sm *sm, char *buf, size_t buflen,
			int verbose);
int eapol_sm_get_mib(struct eapol_sm *sm, char *buf, size_t buflen);
void eapol_sm_configure(struct eapol_sm *sm, int heldPeriod, int authPeriod,
			int startPeriod, int maxStart);
int eapol_sm_rx_eapol(struct eapol_sm *sm, const u8 *src, const u8 *buf,
		      size_t len);
void eapol_sm_notify_tx_eapol_key(struct eapol_sm *sm);
void eapol_sm_notify_portEnabled(struct eapol_sm *sm, Boolean enabled);
void eapol_sm_notify_portValid(struct eapol_sm *sm, Boolean valid);
void eapol_sm_notify_eap_success(struct eapol_sm *sm, Boolean success);
void eapol_sm_notify_eap_fail(struct eapol_sm *sm, Boolean fail);
void eapol_sm_notify_config(struct eapol_sm *sm,
			    struct eap_peer_config *config,
			    const struct eapol_config *conf);
int eapol_sm_get_key(struct eapol_sm *sm, u8 *key, size_t len);
const u8 * eapol_sm_get_session_id(struct eapol_sm *sm, size_t *len);
void eapol_sm_notify_logoff(struct eapol_sm *sm, Boolean logoff);
void eapol_sm_notify_cached(struct eapol_sm *sm);
void eapol_sm_notify_pmkid_attempt(struct eapol_sm *sm);
void eapol_sm_register_scard_ctx(struct eapol_sm *sm, void *ctx);
void eapol_sm_notify_portControl(struct eapol_sm *sm, PortControl portControl);
void eapol_sm_notify_ctrl_attached(struct eapol_sm *sm);
void eapol_sm_notify_ctrl_response(struct eapol_sm *sm);
void eapol_sm_request_reauth(struct eapol_sm *sm);
void eapol_sm_notify_lower_layer_success(struct eapol_sm *sm, int in_eapol_sm);
void eapol_sm_invalidate_cached_session(struct eapol_sm *sm);
const char * eapol_sm_get_method_name(struct eapol_sm *sm);
void eapol_sm_set_ext_pw_ctx(struct eapol_sm *sm,
			     struct ext_password_data *ext);
int eapol_sm_failed(struct eapol_sm *sm);
void eapol_sm_erp_flush(struct eapol_sm *sm);
struct wpabuf * eapol_sm_build_erp_reauth_start(struct eapol_sm *sm);
void eapol_sm_process_erp_finish(struct eapol_sm *sm, const u8 *buf,
				 size_t len);
int eapol_sm_get_eap_proxy_imsi(void *ctx, int sim_num, char *imsi,
				size_t *len);
int eapol_sm_update_erp_next_seq_num(struct eapol_sm *sm, u16 next_seq_num);
int eapol_sm_get_erp_info(struct eapol_sm *sm, struct eap_peer_config *config,
			  const u8 **username, size_t *username_len,
			  const u8 **realm, size_t *realm_len,
			  u16 *erp_next_seq_num, const u8 **rrk,
			  size_t *rrk_len);

#else /* IEEE8021X_EAPOL */
static inline struct eapol_sm *eapol_sm_init(struct eapol_ctx *ctx)
{
	free(ctx);
	return (struct eapol_sm *) 1;
}
static inline void eapol_sm_deinit(struct eapol_sm *sm)
{
}
static inline void eapol_sm_step(struct eapol_sm *sm)
{
}
static inline int eapol_sm_get_status(struct eapol_sm *sm, char *buf,
				      size_t buflen, int verbose)
{
	return 0;
}
static inline int eapol_sm_get_mib(struct eapol_sm *sm, char *buf,
				   size_t buflen)
{
	return 0;
}
static inline void eapol_sm_configure(struct eapol_sm *sm, int heldPeriod,
				      int authPeriod, int startPeriod,
				      int maxStart)
{
}
static inline int eapol_sm_rx_eapol(struct eapol_sm *sm, const u8 *src,
				    const u8 *buf, size_t len)
{
	return 0;
}
static inline void eapol_sm_notify_tx_eapol_key(struct eapol_sm *sm)
{
}
static inline void eapol_sm_notify_portEnabled(struct eapol_sm *sm,
					       Boolean enabled)
{
}
static inline void eapol_sm_notify_portValid(struct eapol_sm *sm,
					     Boolean valid)
{
}
static inline void eapol_sm_notify_eap_success(struct eapol_sm *sm,
					       Boolean success)
{
}
static inline void eapol_sm_notify_eap_fail(struct eapol_sm *sm, Boolean fail)
{
}
static inline void eapol_sm_notify_config(struct eapol_sm *sm,
					  struct eap_peer_config *config,
					  struct eapol_config *conf)
{
}
static inline int eapol_sm_get_key(struct eapol_sm *sm, u8 *key, size_t len)
{
	return -1;
}
static inline const u8 *
eapol_sm_get_session_id(struct eapol_sm *sm, size_t *len)
{
	return NULL;
}
static inline void eapol_sm_notify_logoff(struct eapol_sm *sm, Boolean logoff)
{
}
static inline void eapol_sm_notify_cached(struct eapol_sm *sm)
{
}
static inline void eapol_sm_notify_pmkid_attempt(struct eapol_sm *sm)
{
}
#define eapol_sm_register_scard_ctx(sm, ctx) do { } while (0)
static inline void eapol_sm_notify_portControl(struct eapol_sm *sm,
					       PortControl portControl)
{
}
static inline void eapol_sm_notify_ctrl_attached(struct eapol_sm *sm)
{
}
static inline void eapol_sm_notify_ctrl_response(struct eapol_sm *sm)
{
}
static inline void eapol_sm_request_reauth(struct eapol_sm *sm)
{
}
static inline void eapol_sm_notify_lower_layer_success(struct eapol_sm *sm,
						       int in_eapol_sm)
{
}
static inline void eapol_sm_invalidate_cached_session(struct eapol_sm *sm)
{
}
static inline const char * eapol_sm_get_method_name(struct eapol_sm *sm)
{
	return NULL;
}
static inline void eapol_sm_set_ext_pw_ctx(struct eapol_sm *sm,
					   struct ext_password_data *ext)
{
}
static inline int eapol_sm_failed(struct eapol_sm *sm)
{
	return 0;
}
static inline void eapol_sm_erp_flush(struct eapol_sm *sm)
{
}
static inline struct wpabuf *
eapol_sm_build_erp_reauth_start(struct eapol_sm *sm)
{
	return NULL;
}
static inline void eapol_sm_process_erp_finish(struct eapol_sm *sm,
					       const u8 *buf, size_t len)
{
}
static inline int eapol_sm_update_erp_next_seq_num(struct eapol_sm *sm,
						   u16 next_seq_num)
{
	return -1;
}
static inline int
eapol_sm_get_erp_info(struct eapol_sm *sm, struct eap_peer_config *config,
		      const u8 **username, size_t *username_len,
		      const u8 **realm, size_t *realm_len,
		      u16 *erp_next_seq_num, const u8 **rrk, size_t *rrk_len)
{
	return -1;
}
#endif /* IEEE8021X_EAPOL */

#endif /* EAPOL_SUPP_SM_H */
