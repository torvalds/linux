/*
 * DPP functionality shared between hostapd and wpa_supplicant
 * Copyright (c) 2017, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef DPP_H
#define DPP_H

#include <openssl/x509.h>

#include "utils/list.h"
#include "common/wpa_common.h"
#include "crypto/sha256.h"

#define DPP_HDR_LEN (4 + 2) /* OUI, OUI Type, Crypto Suite, DPP frame type */

enum dpp_public_action_frame_type {
	DPP_PA_AUTHENTICATION_REQ = 0,
	DPP_PA_AUTHENTICATION_RESP = 1,
	DPP_PA_AUTHENTICATION_CONF = 2,
	DPP_PA_PEER_DISCOVERY_REQ = 5,
	DPP_PA_PEER_DISCOVERY_RESP = 6,
	DPP_PA_PKEX_EXCHANGE_REQ = 7,
	DPP_PA_PKEX_EXCHANGE_RESP = 8,
	DPP_PA_PKEX_COMMIT_REVEAL_REQ = 9,
	DPP_PA_PKEX_COMMIT_REVEAL_RESP = 10,
};

enum dpp_attribute_id {
	DPP_ATTR_STATUS = 0x1000,
	DPP_ATTR_I_BOOTSTRAP_KEY_HASH = 0x1001,
	DPP_ATTR_R_BOOTSTRAP_KEY_HASH = 0x1002,
	DPP_ATTR_I_PROTOCOL_KEY = 0x1003,
	DPP_ATTR_WRAPPED_DATA = 0x1004,
	DPP_ATTR_I_NONCE = 0x1005,
	DPP_ATTR_I_CAPABILITIES = 0x1006,
	DPP_ATTR_R_NONCE = 0x1007,
	DPP_ATTR_R_CAPABILITIES = 0x1008,
	DPP_ATTR_R_PROTOCOL_KEY = 0x1009,
	DPP_ATTR_I_AUTH_TAG = 0x100A,
	DPP_ATTR_R_AUTH_TAG = 0x100B,
	DPP_ATTR_CONFIG_OBJ = 0x100C,
	DPP_ATTR_CONNECTOR = 0x100D,
	DPP_ATTR_CONFIG_ATTR_OBJ = 0x100E,
	DPP_ATTR_BOOTSTRAP_KEY = 0x100F,
	DPP_ATTR_OWN_NET_NK_HASH = 0x1011,
	DPP_ATTR_FINITE_CYCLIC_GROUP = 0x1012,
	DPP_ATTR_ENCRYPTED_KEY = 0x1013,
	DPP_ATTR_ENROLLEE_NONCE = 0x1014,
	DPP_ATTR_CODE_IDENTIFIER = 0x1015,
	DPP_ATTR_TRANSACTION_ID = 0x1016,
	DPP_ATTR_BOOTSTRAP_INFO = 0x1017,
	DPP_ATTR_CHANNEL = 0x1018,
};

enum dpp_status_error {
	DPP_STATUS_OK = 0,
	DPP_STATUS_NOT_COMPATIBLE = 1,
	DPP_STATUS_AUTH_FAILURE = 2,
	DPP_STATUS_UNWRAP_FAILURE = 3,
	DPP_STATUS_BAD_GROUP = 4,
	DPP_STATUS_CONFIGURE_FAILURE = 5,
	DPP_STATUS_RESPONSE_PENDING = 6,
	DPP_STATUS_INVALID_CONNECTOR = 7,
	DPP_STATUS_NO_MATCH = 8,
};

#define DPP_CAPAB_ENROLLEE BIT(0)
#define DPP_CAPAB_CONFIGURATOR BIT(1)
#define DPP_CAPAB_ROLE_MASK (BIT(0) | BIT(1))

#define DPP_BOOTSTRAP_MAX_FREQ 30
#define DPP_MAX_NONCE_LEN 32
#define DPP_MAX_HASH_LEN 64
#define DPP_MAX_SHARED_SECRET_LEN 66

struct dpp_curve_params {
	const char *name;
	size_t hash_len;
	size_t aes_siv_key_len;
	size_t nonce_len;
	size_t prime_len;
	const char *jwk_crv;
	u16 ike_group;
	const char *jws_alg;
};

enum dpp_bootstrap_type {
	DPP_BOOTSTRAP_QR_CODE,
	DPP_BOOTSTRAP_PKEX,
};

struct dpp_bootstrap_info {
	struct dl_list list;
	unsigned int id;
	enum dpp_bootstrap_type type;
	char *uri;
	u8 mac_addr[ETH_ALEN];
	char *info;
	unsigned int freq[DPP_BOOTSTRAP_MAX_FREQ];
	unsigned int num_freq;
	int own;
	EVP_PKEY *pubkey;
	u8 pubkey_hash[SHA256_MAC_LEN];
	const struct dpp_curve_params *curve;
	unsigned int pkex_t; /* number of failures before dpp_pkex
			      * instantiation */
};

#define PKEX_COUNTER_T_LIMIT 5

struct dpp_pkex {
	void *msg_ctx;
	unsigned int initiator:1;
	unsigned int exchange_done:1;
	unsigned int failed:1;
	struct dpp_bootstrap_info *own_bi;
	u8 own_mac[ETH_ALEN];
	u8 peer_mac[ETH_ALEN];
	char *identifier;
	char *code;
	EVP_PKEY *x;
	EVP_PKEY *y;
	u8 Mx[DPP_MAX_SHARED_SECRET_LEN];
	u8 Nx[DPP_MAX_SHARED_SECRET_LEN];
	u8 z[DPP_MAX_HASH_LEN];
	EVP_PKEY *peer_bootstrap_key;
	struct wpabuf *exchange_req;
	struct wpabuf *exchange_resp;
	unsigned int t; /* number of failures on code use */
	unsigned int exch_req_wait_time;
	unsigned int exch_req_tries;
	unsigned int freq;
};

enum dpp_akm {
	DPP_AKM_UNKNOWN,
	DPP_AKM_DPP,
	DPP_AKM_PSK,
	DPP_AKM_SAE,
	DPP_AKM_PSK_SAE
};

struct dpp_configuration {
	u8 ssid[32];
	size_t ssid_len;
	enum dpp_akm akm;

	/* For DPP configuration (connector) */
	os_time_t netaccesskey_expiry;

	/* TODO: groups */
	char *group_id;

	/* For legacy configuration */
	char *passphrase;
	u8 psk[32];
};

struct dpp_authentication {
	void *msg_ctx;
	const struct dpp_curve_params *curve;
	struct dpp_bootstrap_info *peer_bi;
	struct dpp_bootstrap_info *own_bi;
	struct dpp_bootstrap_info *tmp_own_bi;
	u8 waiting_pubkey_hash[SHA256_MAC_LEN];
	int response_pending;
	enum dpp_status_error auth_resp_status;
	u8 peer_mac_addr[ETH_ALEN];
	u8 i_nonce[DPP_MAX_NONCE_LEN];
	u8 r_nonce[DPP_MAX_NONCE_LEN];
	u8 e_nonce[DPP_MAX_NONCE_LEN];
	u8 i_capab;
	u8 r_capab;
	EVP_PKEY *own_protocol_key;
	EVP_PKEY *peer_protocol_key;
	struct wpabuf *req_msg;
	struct wpabuf *resp_msg;
	/* Intersection of possible frequencies for initiating DPP
	 * Authentication exchange */
	unsigned int freq[DPP_BOOTSTRAP_MAX_FREQ];
	unsigned int num_freq, freq_idx;
	unsigned int curr_freq;
	unsigned int neg_freq;
	unsigned int num_freq_iters;
	size_t secret_len;
	u8 Mx[DPP_MAX_SHARED_SECRET_LEN];
	size_t Mx_len;
	u8 Nx[DPP_MAX_SHARED_SECRET_LEN];
	size_t Nx_len;
	u8 Lx[DPP_MAX_SHARED_SECRET_LEN];
	size_t Lx_len;
	u8 k1[DPP_MAX_HASH_LEN];
	u8 k2[DPP_MAX_HASH_LEN];
	u8 ke[DPP_MAX_HASH_LEN];
	int initiator;
	int waiting_auth_resp;
	int waiting_auth_conf;
	int auth_req_ack;
	unsigned int auth_resp_tries;
	u8 allowed_roles;
	int configurator;
	int remove_on_tx_status;
	int auth_success;
	struct wpabuf *conf_req;
	const struct wpabuf *conf_resp; /* owned by GAS server */
	struct dpp_configuration *conf_ap;
	struct dpp_configuration *conf_sta;
	struct dpp_configurator *conf;
	char *connector; /* received signedConnector */
	u8 ssid[SSID_MAX_LEN];
	u8 ssid_len;
	char passphrase[64];
	u8 psk[PMK_LEN];
	int psk_set;
	enum dpp_akm akm;
	struct wpabuf *net_access_key;
	os_time_t net_access_key_expiry;
	struct wpabuf *c_sign_key;
#ifdef CONFIG_TESTING_OPTIONS
	char *config_obj_override;
	char *discovery_override;
	char *groups_override;
	unsigned int ignore_netaccesskey_mismatch:1;
#endif /* CONFIG_TESTING_OPTIONS */
};

struct dpp_configurator {
	struct dl_list list;
	unsigned int id;
	int own;
	EVP_PKEY *csign;
	char *kid;
	const struct dpp_curve_params *curve;
};

struct dpp_introduction {
	u8 pmkid[PMKID_LEN];
	u8 pmk[PMK_LEN_MAX];
	size_t pmk_len;
};

#ifdef CONFIG_TESTING_OPTIONS
enum dpp_test_behavior {
	DPP_TEST_DISABLED = 0,
	DPP_TEST_AFTER_WRAPPED_DATA_AUTH_REQ = 1,
	DPP_TEST_AFTER_WRAPPED_DATA_AUTH_RESP = 2,
	DPP_TEST_AFTER_WRAPPED_DATA_AUTH_CONF = 3,
	DPP_TEST_AFTER_WRAPPED_DATA_PKEX_CR_REQ = 4,
	DPP_TEST_AFTER_WRAPPED_DATA_PKEX_CR_RESP = 5,
	DPP_TEST_AFTER_WRAPPED_DATA_CONF_REQ = 6,
	DPP_TEST_AFTER_WRAPPED_DATA_CONF_RESP = 7,
	DPP_TEST_ZERO_I_CAPAB = 8,
	DPP_TEST_ZERO_R_CAPAB = 9,
	DPP_TEST_NO_R_BOOTSTRAP_KEY_HASH_AUTH_REQ = 10,
	DPP_TEST_NO_I_BOOTSTRAP_KEY_HASH_AUTH_REQ = 11,
	DPP_TEST_NO_I_PROTO_KEY_AUTH_REQ = 12,
	DPP_TEST_NO_I_NONCE_AUTH_REQ = 13,
	DPP_TEST_NO_I_CAPAB_AUTH_REQ = 14,
	DPP_TEST_NO_WRAPPED_DATA_AUTH_REQ = 15,
	DPP_TEST_NO_STATUS_AUTH_RESP = 16,
	DPP_TEST_NO_R_BOOTSTRAP_KEY_HASH_AUTH_RESP = 17,
	DPP_TEST_NO_I_BOOTSTRAP_KEY_HASH_AUTH_RESP = 18,
	DPP_TEST_NO_R_PROTO_KEY_AUTH_RESP = 19,
	DPP_TEST_NO_R_NONCE_AUTH_RESP = 20,
	DPP_TEST_NO_I_NONCE_AUTH_RESP = 21,
	DPP_TEST_NO_R_CAPAB_AUTH_RESP = 22,
	DPP_TEST_NO_R_AUTH_AUTH_RESP = 23,
	DPP_TEST_NO_WRAPPED_DATA_AUTH_RESP = 24,
	DPP_TEST_NO_STATUS_AUTH_CONF = 25,
	DPP_TEST_NO_R_BOOTSTRAP_KEY_HASH_AUTH_CONF = 26,
	DPP_TEST_NO_I_BOOTSTRAP_KEY_HASH_AUTH_CONF = 27,
	DPP_TEST_NO_I_AUTH_AUTH_CONF = 28,
	DPP_TEST_NO_WRAPPED_DATA_AUTH_CONF = 29,
	DPP_TEST_I_NONCE_MISMATCH_AUTH_RESP = 30,
	DPP_TEST_INCOMPATIBLE_R_CAPAB_AUTH_RESP = 31,
	DPP_TEST_R_AUTH_MISMATCH_AUTH_RESP = 32,
	DPP_TEST_I_AUTH_MISMATCH_AUTH_CONF = 33,
	DPP_TEST_NO_FINITE_CYCLIC_GROUP_PKEX_EXCHANGE_REQ = 34,
	DPP_TEST_NO_ENCRYPTED_KEY_PKEX_EXCHANGE_REQ = 35,
	DPP_TEST_NO_STATUS_PKEX_EXCHANGE_RESP = 36,
	DPP_TEST_NO_ENCRYPTED_KEY_PKEX_EXCHANGE_RESP = 37,
	DPP_TEST_NO_BOOTSTRAP_KEY_PKEX_CR_REQ = 38,
	DPP_TEST_NO_I_AUTH_TAG_PKEX_CR_REQ = 39,
	DPP_TEST_NO_WRAPPED_DATA_PKEX_CR_REQ = 40,
	DPP_TEST_NO_BOOTSTRAP_KEY_PKEX_CR_RESP = 41,
	DPP_TEST_NO_R_AUTH_TAG_PKEX_CR_RESP = 42,
	DPP_TEST_NO_WRAPPED_DATA_PKEX_CR_RESP = 43,
	DPP_TEST_INVALID_ENCRYPTED_KEY_PKEX_EXCHANGE_REQ = 44,
	DPP_TEST_INVALID_ENCRYPTED_KEY_PKEX_EXCHANGE_RESP = 45,
	DPP_TEST_INVALID_STATUS_PKEX_EXCHANGE_RESP = 46,
	DPP_TEST_INVALID_BOOTSTRAP_KEY_PKEX_CR_REQ = 47,
	DPP_TEST_INVALID_BOOTSTRAP_KEY_PKEX_CR_RESP = 48,
	DPP_TEST_I_AUTH_TAG_MISMATCH_PKEX_CR_REQ = 49,
	DPP_TEST_R_AUTH_TAG_MISMATCH_PKEX_CR_RESP = 50,
	DPP_TEST_NO_E_NONCE_CONF_REQ = 51,
	DPP_TEST_NO_CONFIG_ATTR_OBJ_CONF_REQ = 52,
	DPP_TEST_NO_WRAPPED_DATA_CONF_REQ = 53,
	DPP_TEST_NO_E_NONCE_CONF_RESP = 54,
	DPP_TEST_NO_CONFIG_OBJ_CONF_RESP = 55,
	DPP_TEST_NO_STATUS_CONF_RESP = 56,
	DPP_TEST_NO_WRAPPED_DATA_CONF_RESP = 57,
	DPP_TEST_INVALID_STATUS_CONF_RESP = 58,
	DPP_TEST_E_NONCE_MISMATCH_CONF_RESP = 59,
	DPP_TEST_NO_TRANSACTION_ID_PEER_DISC_REQ = 60,
	DPP_TEST_NO_CONNECTOR_PEER_DISC_REQ = 61,
	DPP_TEST_NO_TRANSACTION_ID_PEER_DISC_RESP = 62,
	DPP_TEST_NO_STATUS_PEER_DISC_RESP = 63,
	DPP_TEST_NO_CONNECTOR_PEER_DISC_RESP = 64,
	DPP_TEST_AUTH_RESP_IN_PLACE_OF_CONF = 65,
	DPP_TEST_INVALID_I_PROTO_KEY_AUTH_REQ = 66,
	DPP_TEST_INVALID_R_PROTO_KEY_AUTH_RESP = 67,
	DPP_TEST_INVALID_R_BOOTSTRAP_KEY_HASH_AUTH_REQ = 68,
	DPP_TEST_INVALID_I_BOOTSTRAP_KEY_HASH_AUTH_REQ = 69,
	DPP_TEST_INVALID_R_BOOTSTRAP_KEY_HASH_AUTH_RESP = 70,
	DPP_TEST_INVALID_I_BOOTSTRAP_KEY_HASH_AUTH_RESP = 71,
	DPP_TEST_INVALID_R_BOOTSTRAP_KEY_HASH_AUTH_CONF = 72,
	DPP_TEST_INVALID_I_BOOTSTRAP_KEY_HASH_AUTH_CONF = 73,
	DPP_TEST_INVALID_STATUS_AUTH_RESP = 74,
	DPP_TEST_INVALID_STATUS_AUTH_CONF = 75,
	DPP_TEST_INVALID_CONFIG_ATTR_OBJ_CONF_REQ = 76,
	DPP_TEST_INVALID_TRANSACTION_ID_PEER_DISC_RESP = 77,
	DPP_TEST_INVALID_STATUS_PEER_DISC_RESP = 78,
	DPP_TEST_INVALID_CONNECTOR_PEER_DISC_RESP = 79,
	DPP_TEST_INVALID_CONNECTOR_PEER_DISC_REQ = 80,
	DPP_TEST_INVALID_I_NONCE_AUTH_REQ = 81,
	DPP_TEST_INVALID_TRANSACTION_ID_PEER_DISC_REQ = 82,
	DPP_TEST_INVALID_E_NONCE_CONF_REQ = 83,
	DPP_TEST_STOP_AT_PKEX_EXCHANGE_RESP = 84,
	DPP_TEST_STOP_AT_PKEX_CR_REQ = 85,
	DPP_TEST_STOP_AT_PKEX_CR_RESP = 86,
	DPP_TEST_STOP_AT_AUTH_REQ = 87,
	DPP_TEST_STOP_AT_AUTH_RESP = 88,
	DPP_TEST_STOP_AT_AUTH_CONF = 89,
	DPP_TEST_STOP_AT_CONF_REQ = 90,
};

extern enum dpp_test_behavior dpp_test;
extern u8 dpp_pkex_own_mac_override[ETH_ALEN];
extern u8 dpp_pkex_peer_mac_override[ETH_ALEN];
extern u8 dpp_pkex_ephemeral_key_override[600];
extern size_t dpp_pkex_ephemeral_key_override_len;
extern u8 dpp_protocol_key_override[600];
extern size_t dpp_protocol_key_override_len;
extern u8 dpp_nonce_override[DPP_MAX_NONCE_LEN];
extern size_t dpp_nonce_override_len;
#endif /* CONFIG_TESTING_OPTIONS */

void dpp_bootstrap_info_free(struct dpp_bootstrap_info *info);
const char * dpp_bootstrap_type_txt(enum dpp_bootstrap_type type);
int dpp_bootstrap_key_hash(struct dpp_bootstrap_info *bi);
int dpp_parse_uri_chan_list(struct dpp_bootstrap_info *bi,
			    const char *chan_list);
int dpp_parse_uri_mac(struct dpp_bootstrap_info *bi, const char *mac);
int dpp_parse_uri_info(struct dpp_bootstrap_info *bi, const char *info);
struct dpp_bootstrap_info * dpp_parse_qr_code(const char *uri);
char * dpp_keygen(struct dpp_bootstrap_info *bi, const char *curve,
		  const u8 *privkey, size_t privkey_len);
struct hostapd_hw_modes;
struct dpp_authentication * dpp_auth_init(void *msg_ctx,
					  struct dpp_bootstrap_info *peer_bi,
					  struct dpp_bootstrap_info *own_bi,
					  u8 dpp_allowed_roles,
					  unsigned int neg_freq,
					  struct hostapd_hw_modes *own_modes,
					  u16 num_modes);
struct dpp_authentication *
dpp_auth_req_rx(void *msg_ctx, u8 dpp_allowed_roles, int qr_mutual,
		struct dpp_bootstrap_info *peer_bi,
		struct dpp_bootstrap_info *own_bi,
		unsigned int freq, const u8 *hdr, const u8 *attr_start,
		size_t attr_len);
struct wpabuf *
dpp_auth_resp_rx(struct dpp_authentication *auth, const u8 *hdr,
		 const u8 *attr_start, size_t attr_len);
struct wpabuf * dpp_build_conf_req(struct dpp_authentication *auth,
				   const char *json);
int dpp_auth_conf_rx(struct dpp_authentication *auth, const u8 *hdr,
		     const u8 *attr_start, size_t attr_len);
int dpp_notify_new_qr_code(struct dpp_authentication *auth,
			   struct dpp_bootstrap_info *peer_bi);
void dpp_configuration_free(struct dpp_configuration *conf);
void dpp_auth_deinit(struct dpp_authentication *auth);
struct wpabuf *
dpp_conf_req_rx(struct dpp_authentication *auth, const u8 *attr_start,
		size_t attr_len);
int dpp_conf_resp_rx(struct dpp_authentication *auth,
		     const struct wpabuf *resp);
struct wpabuf * dpp_alloc_msg(enum dpp_public_action_frame_type type,
			      size_t len);
const u8 * dpp_get_attr(const u8 *buf, size_t len, u16 req_id, u16 *ret_len);
int dpp_check_attrs(const u8 *buf, size_t len);
int dpp_key_expired(const char *timestamp, os_time_t *expiry);
const char * dpp_akm_str(enum dpp_akm akm);
int dpp_configurator_get_key(const struct dpp_configurator *conf, char *buf,
			     size_t buflen);
void dpp_configurator_free(struct dpp_configurator *conf);
struct dpp_configurator *
dpp_keygen_configurator(const char *curve, const u8 *privkey,
			size_t privkey_len);
int dpp_configurator_own_config(struct dpp_authentication *auth,
				const char *curve, int ap);
enum dpp_status_error
dpp_peer_intro(struct dpp_introduction *intro, const char *own_connector,
	       const u8 *net_access_key, size_t net_access_key_len,
	       const u8 *csign_key, size_t csign_key_len,
	       const u8 *peer_connector, size_t peer_connector_len,
	       os_time_t *expiry);
struct dpp_pkex * dpp_pkex_init(void *msg_ctx, struct dpp_bootstrap_info *bi,
				const u8 *own_mac,
				const char *identifier,
				const char *code);
struct dpp_pkex * dpp_pkex_rx_exchange_req(void *msg_ctx,
					   struct dpp_bootstrap_info *bi,
					   const u8 *own_mac,
					   const u8 *peer_mac,
					   const char *identifier,
					   const char *code,
					   const u8 *buf, size_t len);
struct wpabuf * dpp_pkex_rx_exchange_resp(struct dpp_pkex *pkex,
					  const u8 *peer_mac,
					  const u8 *buf, size_t len);
struct wpabuf * dpp_pkex_rx_commit_reveal_req(struct dpp_pkex *pkex,
					      const u8 *hdr,
					      const u8 *buf, size_t len);
int dpp_pkex_rx_commit_reveal_resp(struct dpp_pkex *pkex, const u8 *hdr,
				   const u8 *buf, size_t len);
void dpp_pkex_free(struct dpp_pkex *pkex);

char * dpp_corrupt_connector_signature(const char *connector);

#endif /* DPP_H */
