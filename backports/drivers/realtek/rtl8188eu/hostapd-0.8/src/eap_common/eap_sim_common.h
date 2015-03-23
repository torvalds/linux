/*
 * EAP peer/server: EAP-SIM/AKA/AKA' shared routines
 * Copyright (c) 2004-2008, Jouni Malinen <j@w1.fi>
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

#ifndef EAP_SIM_COMMON_H
#define EAP_SIM_COMMON_H

#define EAP_SIM_NONCE_S_LEN 16
#define EAP_SIM_NONCE_MT_LEN 16
#define EAP_SIM_MAC_LEN 16
#define EAP_SIM_MK_LEN 20
#define EAP_SIM_K_AUT_LEN 16
#define EAP_SIM_K_ENCR_LEN 16
#define EAP_SIM_KEYING_DATA_LEN 64
#define EAP_SIM_IV_LEN 16
#define EAP_SIM_KC_LEN 8
#define EAP_SIM_SRES_LEN 4

#define GSM_RAND_LEN 16

#define EAP_SIM_VERSION 1

/* EAP-SIM Subtypes */
#define EAP_SIM_SUBTYPE_START 10
#define EAP_SIM_SUBTYPE_CHALLENGE 11
#define EAP_SIM_SUBTYPE_NOTIFICATION 12
#define EAP_SIM_SUBTYPE_REAUTHENTICATION 13
#define EAP_SIM_SUBTYPE_CLIENT_ERROR 14

/* AT_CLIENT_ERROR_CODE error codes */
#define EAP_SIM_UNABLE_TO_PROCESS_PACKET 0
#define EAP_SIM_UNSUPPORTED_VERSION 1
#define EAP_SIM_INSUFFICIENT_NUM_OF_CHAL 2
#define EAP_SIM_RAND_NOT_FRESH 3

#define EAP_SIM_MAX_FAST_REAUTHS 1000

#define EAP_SIM_MAX_CHAL 3


/* EAP-AKA Subtypes */
#define EAP_AKA_SUBTYPE_CHALLENGE 1
#define EAP_AKA_SUBTYPE_AUTHENTICATION_REJECT 2
#define EAP_AKA_SUBTYPE_SYNCHRONIZATION_FAILURE 4
#define EAP_AKA_SUBTYPE_IDENTITY 5
#define EAP_AKA_SUBTYPE_NOTIFICATION 12
#define EAP_AKA_SUBTYPE_REAUTHENTICATION 13
#define EAP_AKA_SUBTYPE_CLIENT_ERROR 14

/* AT_CLIENT_ERROR_CODE error codes */
#define EAP_AKA_UNABLE_TO_PROCESS_PACKET 0

#define EAP_AKA_RAND_LEN 16
#define EAP_AKA_AUTN_LEN 16
#define EAP_AKA_AUTS_LEN 14
#define EAP_AKA_RES_MAX_LEN 16
#define EAP_AKA_IK_LEN 16
#define EAP_AKA_CK_LEN 16
#define EAP_AKA_MAX_FAST_REAUTHS 1000
#define EAP_AKA_MIN_RES_LEN 4
#define EAP_AKA_MAX_RES_LEN 16
#define EAP_AKA_CHECKCODE_LEN 20

#define EAP_AKA_PRIME_K_AUT_LEN 32
#define EAP_AKA_PRIME_CHECKCODE_LEN 32
#define EAP_AKA_PRIME_K_RE_LEN 32

struct wpabuf;

void eap_sim_derive_mk(const u8 *identity, size_t identity_len,
		       const u8 *nonce_mt, u16 selected_version,
		       const u8 *ver_list, size_t ver_list_len,
		       int num_chal, const u8 *kc, u8 *mk);
void eap_aka_derive_mk(const u8 *identity, size_t identity_len,
		       const u8 *ik, const u8 *ck, u8 *mk);
int eap_sim_derive_keys(const u8 *mk, u8 *k_encr, u8 *k_aut, u8 *msk,
			u8 *emsk);
int eap_sim_derive_keys_reauth(u16 _counter,
			       const u8 *identity, size_t identity_len,
			       const u8 *nonce_s, const u8 *mk, u8 *msk,
			       u8 *emsk);
int eap_sim_verify_mac(const u8 *k_aut, const struct wpabuf *req,
		       const u8 *mac, const u8 *extra, size_t extra_len);
void eap_sim_add_mac(const u8 *k_aut, const u8 *msg, size_t msg_len, u8 *mac,
		     const u8 *extra, size_t extra_len);

#if defined(EAP_AKA_PRIME) || defined(EAP_SERVER_AKA_PRIME)
void eap_aka_prime_derive_keys(const u8 *identity, size_t identity_len,
			       const u8 *ik, const u8 *ck, u8 *k_encr,
			       u8 *k_aut, u8 *k_re, u8 *msk, u8 *emsk);
int eap_aka_prime_derive_keys_reauth(const u8 *k_re, u16 counter,
				     const u8 *identity, size_t identity_len,
				     const u8 *nonce_s, u8 *msk, u8 *emsk);
int eap_sim_verify_mac_sha256(const u8 *k_aut, const struct wpabuf *req,
			      const u8 *mac, const u8 *extra,
			      size_t extra_len);
void eap_sim_add_mac_sha256(const u8 *k_aut, const u8 *msg, size_t msg_len,
			    u8 *mac, const u8 *extra, size_t extra_len);

void eap_aka_prime_derive_ck_ik_prime(u8 *ck, u8 *ik, const u8 *sqn_ak,
				      const u8 *network_name,
				      size_t network_name_len);
#else /* EAP_AKA_PRIME || EAP_SERVER_AKA_PRIME */
static inline void eap_aka_prime_derive_keys(const u8 *identity,
					     size_t identity_len,
					     const u8 *ik, const u8 *ck,
					     u8 *k_encr, u8 *k_aut, u8 *k_re,
					     u8 *msk, u8 *emsk)
{
}

static inline int eap_aka_prime_derive_keys_reauth(const u8 *k_re, u16 counter,
						   const u8 *identity,
						   size_t identity_len,
						   const u8 *nonce_s, u8 *msk,
						   u8 *emsk)
{
	return -1;
}

static inline int eap_sim_verify_mac_sha256(const u8 *k_aut,
					    const struct wpabuf *req,
					    const u8 *mac, const u8 *extra,
					    size_t extra_len)
{
	return -1;
}
#endif /* EAP_AKA_PRIME || EAP_SERVER_AKA_PRIME */


/* EAP-SIM/AKA Attributes (0..127 non-skippable) */
#define EAP_SIM_AT_RAND 1
#define EAP_SIM_AT_AUTN 2 /* only AKA */
#define EAP_SIM_AT_RES 3 /* only AKA, only peer->server */
#define EAP_SIM_AT_AUTS 4 /* only AKA, only peer->server */
#define EAP_SIM_AT_PADDING 6 /* only encrypted */
#define EAP_SIM_AT_NONCE_MT 7 /* only SIM, only send */
#define EAP_SIM_AT_PERMANENT_ID_REQ 10
#define EAP_SIM_AT_MAC 11
#define EAP_SIM_AT_NOTIFICATION 12
#define EAP_SIM_AT_ANY_ID_REQ 13
#define EAP_SIM_AT_IDENTITY 14 /* only send */
#define EAP_SIM_AT_VERSION_LIST 15 /* only SIM */
#define EAP_SIM_AT_SELECTED_VERSION 16 /* only SIM */
#define EAP_SIM_AT_FULLAUTH_ID_REQ 17
#define EAP_SIM_AT_COUNTER 19 /* only encrypted */
#define EAP_SIM_AT_COUNTER_TOO_SMALL 20 /* only encrypted */
#define EAP_SIM_AT_NONCE_S 21 /* only encrypted */
#define EAP_SIM_AT_CLIENT_ERROR_CODE 22 /* only send */
#define EAP_SIM_AT_KDF_INPUT 23 /* only AKA' */
#define EAP_SIM_AT_KDF 24 /* only AKA' */
#define EAP_SIM_AT_IV 129
#define EAP_SIM_AT_ENCR_DATA 130
#define EAP_SIM_AT_NEXT_PSEUDONYM 132 /* only encrypted */
#define EAP_SIM_AT_NEXT_REAUTH_ID 133 /* only encrypted */
#define EAP_SIM_AT_CHECKCODE 134 /* only AKA */
#define EAP_SIM_AT_RESULT_IND 135
#define EAP_SIM_AT_BIDDING 136

/* AT_NOTIFICATION notification code values */
#define EAP_SIM_GENERAL_FAILURE_AFTER_AUTH 0
#define EAP_SIM_TEMPORARILY_DENIED 1026
#define EAP_SIM_NOT_SUBSCRIBED 1031
#define EAP_SIM_GENERAL_FAILURE_BEFORE_AUTH 16384
#define EAP_SIM_SUCCESS 32768

/* EAP-AKA' AT_KDF Key Derivation Function values */
#define EAP_AKA_PRIME_KDF 1

/* AT_BIDDING flags */
#define EAP_AKA_BIDDING_FLAG_D 0x8000


enum eap_sim_id_req {
	NO_ID_REQ, ANY_ID, FULLAUTH_ID, PERMANENT_ID
};


struct eap_sim_attrs {
	const u8 *rand, *autn, *mac, *iv, *encr_data, *version_list, *nonce_s;
	const u8 *next_pseudonym, *next_reauth_id;
	const u8 *nonce_mt, *identity, *res, *auts;
	const u8 *checkcode;
	const u8 *kdf_input;
	const u8 *bidding;
	size_t num_chal, version_list_len, encr_data_len;
	size_t next_pseudonym_len, next_reauth_id_len, identity_len, res_len;
	size_t res_len_bits;
	size_t checkcode_len;
	size_t kdf_input_len;
	enum eap_sim_id_req id_req;
	int notification, counter, selected_version, client_error_code;
	int counter_too_small;
	int result_ind;
#define EAP_AKA_PRIME_KDF_MAX 10
	u16 kdf[EAP_AKA_PRIME_KDF_MAX];
	size_t kdf_count;
};

int eap_sim_parse_attr(const u8 *start, const u8 *end,
		       struct eap_sim_attrs *attr, int aka, int encr);
u8 * eap_sim_parse_encr(const u8 *k_encr, const u8 *encr_data,
			size_t encr_data_len, const u8 *iv,
			struct eap_sim_attrs *attr, int aka);


struct eap_sim_msg;

struct eap_sim_msg * eap_sim_msg_init(int code, int id, int type, int subtype);
struct wpabuf * eap_sim_msg_finish(struct eap_sim_msg *msg, const u8 *k_aut,
				   const u8 *extra, size_t extra_len);
void eap_sim_msg_free(struct eap_sim_msg *msg);
u8 * eap_sim_msg_add_full(struct eap_sim_msg *msg, u8 attr,
			  const u8 *data, size_t len);
u8 * eap_sim_msg_add(struct eap_sim_msg *msg, u8 attr,
		     u16 value, const u8 *data, size_t len);
u8 * eap_sim_msg_add_mac(struct eap_sim_msg *msg, u8 attr);
int eap_sim_msg_add_encr_start(struct eap_sim_msg *msg, u8 attr_iv,
			       u8 attr_encr);
int eap_sim_msg_add_encr_end(struct eap_sim_msg *msg, u8 *k_encr,
			     int attr_pad);

void eap_sim_report_notification(void *msg_ctx, int notification, int aka);

#endif /* EAP_SIM_COMMON_H */
