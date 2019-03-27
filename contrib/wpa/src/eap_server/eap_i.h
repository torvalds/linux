/*
 * hostapd / EAP Authenticator state machine internal structures (RFC 4137)
 * Copyright (c) 2004-2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAP_I_H
#define EAP_I_H

#include "wpabuf.h"
#include "eap_server/eap.h"
#include "eap_common/eap_common.h"

/* RFC 4137 - EAP Standalone Authenticator */

/**
 * struct eap_method - EAP method interface
 * This structure defines the EAP method interface. Each method will need to
 * register its own EAP type, EAP name, and set of function pointers for method
 * specific operations. This interface is based on section 5.4 of RFC 4137.
 */
struct eap_method {
	int vendor;
	EapType method;
	const char *name;

	void * (*init)(struct eap_sm *sm);
	void * (*initPickUp)(struct eap_sm *sm);
	void (*reset)(struct eap_sm *sm, void *priv);

	struct wpabuf * (*buildReq)(struct eap_sm *sm, void *priv, u8 id);
	int (*getTimeout)(struct eap_sm *sm, void *priv);
	Boolean (*check)(struct eap_sm *sm, void *priv,
			 struct wpabuf *respData);
	void (*process)(struct eap_sm *sm, void *priv,
			struct wpabuf *respData);
	Boolean (*isDone)(struct eap_sm *sm, void *priv);
	u8 * (*getKey)(struct eap_sm *sm, void *priv, size_t *len);
	/* isSuccess is not specified in draft-ietf-eap-statemachine-05.txt,
	 * but it is useful in implementing Policy.getDecision() */
	Boolean (*isSuccess)(struct eap_sm *sm, void *priv);

	/**
	 * free - Free EAP method data
	 * @method: Pointer to the method data registered with
	 * eap_server_method_register().
	 *
	 * This function will be called when the EAP method is being
	 * unregistered. If the EAP method allocated resources during
	 * registration (e.g., allocated struct eap_method), they should be
	 * freed in this function. No other method functions will be called
	 * after this call. If this function is not defined (i.e., function
	 * pointer is %NULL), a default handler is used to release the method
	 * data with free(method). This is suitable for most cases.
	 */
	void (*free)(struct eap_method *method);

#define EAP_SERVER_METHOD_INTERFACE_VERSION 1
	/**
	 * version - Version of the EAP server method interface
	 *
	 * The EAP server method implementation should set this variable to
	 * EAP_SERVER_METHOD_INTERFACE_VERSION. This is used to verify that the
	 * EAP method is using supported API version when using dynamically
	 * loadable EAP methods.
	 */
	int version;

	/**
	 * next - Pointer to the next EAP method
	 *
	 * This variable is used internally in the EAP method registration code
	 * to create a linked list of registered EAP methods.
	 */
	struct eap_method *next;

	/**
	 * get_emsk - Get EAP method specific keying extended material (EMSK)
	 * @sm: Pointer to EAP state machine allocated with eap_sm_init()
	 * @priv: Pointer to private EAP method data from eap_method::init()
	 * @len: Pointer to a variable to store EMSK length
	 * Returns: EMSK or %NULL if not available
	 *
	 * This function can be used to get the extended keying material from
	 * the EAP method. The key may already be stored in the method-specific
	 * private data or this function may derive the key.
	 */
	u8 * (*get_emsk)(struct eap_sm *sm, void *priv, size_t *len);

	/**
	 * getSessionId - Get EAP method specific Session-Id
	 * @sm: Pointer to EAP state machine allocated with eap_server_sm_init()
	 * @priv: Pointer to private EAP method data from eap_method::init()
	 * @len: Pointer to a variable to store Session-Id length
	 * Returns: Session-Id or %NULL if not available
	 *
	 * This function can be used to get the Session-Id from the EAP method.
	 * The Session-Id may already be stored in the method-specific private
	 * data or this function may derive the Session-Id.
	 */
	u8 * (*getSessionId)(struct eap_sm *sm, void *priv, size_t *len);
};

/**
 * struct eap_sm - EAP server state machine data
 */
struct eap_sm {
	enum {
		EAP_DISABLED, EAP_INITIALIZE, EAP_IDLE, EAP_RECEIVED,
		EAP_INTEGRITY_CHECK, EAP_METHOD_RESPONSE, EAP_METHOD_REQUEST,
		EAP_PROPOSE_METHOD, EAP_SELECT_ACTION, EAP_SEND_REQUEST,
		EAP_DISCARD, EAP_NAK, EAP_RETRANSMIT, EAP_SUCCESS, EAP_FAILURE,
		EAP_TIMEOUT_FAILURE, EAP_PICK_UP_METHOD,
		EAP_INITIALIZE_PASSTHROUGH, EAP_IDLE2, EAP_RETRANSMIT2,
		EAP_RECEIVED2, EAP_DISCARD2, EAP_SEND_REQUEST2,
		EAP_AAA_REQUEST, EAP_AAA_RESPONSE, EAP_AAA_IDLE,
		EAP_TIMEOUT_FAILURE2, EAP_FAILURE2, EAP_SUCCESS2,
		EAP_INITIATE_REAUTH_START, EAP_INITIATE_RECEIVED
	} EAP_state;

	/* Constants */
	int MaxRetrans;

	struct eap_eapol_interface eap_if;

	/* Full authenticator state machine local variables */

	/* Long-term (maintained between packets) */
	EapType currentMethod;
	int currentId;
	enum {
		METHOD_PROPOSED, METHOD_CONTINUE, METHOD_END
	} methodState;
	int retransCount;
	struct wpabuf *lastReqData;
	int methodTimeout;

	/* Short-term (not maintained between packets) */
	Boolean rxResp;
	Boolean rxInitiate;
	int respId;
	EapType respMethod;
	int respVendor;
	u32 respVendorMethod;
	Boolean ignore;
	enum {
		DECISION_SUCCESS, DECISION_FAILURE, DECISION_CONTINUE,
		DECISION_PASSTHROUGH, DECISION_INITIATE_REAUTH_START
	} decision;

	/* Miscellaneous variables */
	const struct eap_method *m; /* selected EAP method */
	/* not defined in RFC 4137 */
	Boolean changed;
	void *eapol_ctx, *msg_ctx;
	const struct eapol_callbacks *eapol_cb;
	void *eap_method_priv;
	u8 *identity;
	size_t identity_len;
	char *serial_num;
	/* Whether Phase 2 method should validate identity match */
	int require_identity_match;
	int lastId; /* Identifier used in the last EAP-Packet */
	struct eap_user *user;
	int user_eap_method_index;
	int init_phase2;
	void *ssl_ctx;
	struct eap_sim_db_data *eap_sim_db_priv;
	Boolean backend_auth;
	Boolean update_user;
	int eap_server;

	int num_rounds;
	enum {
		METHOD_PENDING_NONE, METHOD_PENDING_WAIT, METHOD_PENDING_CONT
	} method_pending;

	u8 *auth_challenge;
	u8 *peer_challenge;

	u8 *pac_opaque_encr_key;
	u8 *eap_fast_a_id;
	size_t eap_fast_a_id_len;
	char *eap_fast_a_id_info;
	enum {
		NO_PROV, ANON_PROV, AUTH_PROV, BOTH_PROV
	} eap_fast_prov;
	int pac_key_lifetime;
	int pac_key_refresh_time;
	int eap_sim_aka_result_ind;
	int tnc;
	u16 pwd_group;
	struct wps_context *wps;
	struct wpabuf *assoc_wps_ie;
	struct wpabuf *assoc_p2p_ie;

	Boolean start_reauth;

	u8 peer_addr[ETH_ALEN];

	/* Fragmentation size for EAP method init() handler */
	int fragment_size;

	int pbc_in_m1;

	const u8 *server_id;
	size_t server_id_len;

	Boolean initiate_reauth_start_sent;
	Boolean try_initiate_reauth;
	int erp;
	unsigned int tls_session_lifetime;
	unsigned int tls_flags;

#ifdef CONFIG_TESTING_OPTIONS
	u32 tls_test_flags;
#endif /* CONFIG_TESTING_OPTIONS */
};

int eap_user_get(struct eap_sm *sm, const u8 *identity, size_t identity_len,
		 int phase2);
void eap_log_msg(struct eap_sm *sm, const char *fmt, ...)
PRINTF_FORMAT(2, 3);
void eap_sm_process_nak(struct eap_sm *sm, const u8 *nak_list, size_t len);

#endif /* EAP_I_H */
