/*
 * hostapd / EAP-SIM database/authenticator gateway
 * Copyright (c) 2005-2008, 2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAP_SIM_DB_H
#define EAP_SIM_DB_H

#include "eap_common/eap_sim_common.h"

/* Identity prefixes */
#define EAP_SIM_PERMANENT_PREFIX '1'
#define EAP_SIM_PSEUDONYM_PREFIX '3'
#define EAP_SIM_REAUTH_ID_PREFIX '5'
#define EAP_AKA_PERMANENT_PREFIX '0'
#define EAP_AKA_PSEUDONYM_PREFIX '2'
#define EAP_AKA_REAUTH_ID_PREFIX '4'
#define EAP_AKA_PRIME_PERMANENT_PREFIX '6'
#define EAP_AKA_PRIME_PSEUDONYM_PREFIX '7'
#define EAP_AKA_PRIME_REAUTH_ID_PREFIX '8'

enum eap_sim_db_method {
	EAP_SIM_DB_SIM,
	EAP_SIM_DB_AKA,
	EAP_SIM_DB_AKA_PRIME
};

struct eap_sim_db_data;

struct eap_sim_db_data *
eap_sim_db_init(const char *config, unsigned int db_timeout,
		void (*get_complete_cb)(void *ctx, void *session_ctx),
		void *ctx);

void eap_sim_db_deinit(void *priv);

int eap_sim_db_get_gsm_triplets(struct eap_sim_db_data *data,
				const char *username, int max_chal,
				u8 *_rand, u8 *kc, u8 *sres,
				void *cb_session_ctx);

#define EAP_SIM_DB_FAILURE -1
#define EAP_SIM_DB_PENDING -2

char * eap_sim_db_get_next_pseudonym(struct eap_sim_db_data *data,
				     enum eap_sim_db_method method);

char * eap_sim_db_get_next_reauth_id(struct eap_sim_db_data *data,
				     enum eap_sim_db_method method);

int eap_sim_db_add_pseudonym(struct eap_sim_db_data *data,
			     const char *permanent, char *pseudonym);

int eap_sim_db_add_reauth(struct eap_sim_db_data *data, const char *permanent,
			  char *reauth_id, u16 counter, const u8 *mk);
int eap_sim_db_add_reauth_prime(struct eap_sim_db_data *data,
				const char *permanent,
				char *reauth_id, u16 counter, const u8 *k_encr,
				const u8 *k_aut, const u8 *k_re);

const char * eap_sim_db_get_permanent(struct eap_sim_db_data *data,
				      const char *pseudonym);

struct eap_sim_reauth {
	struct eap_sim_reauth *next;
	char *permanent; /* Permanent username */
	char *reauth_id; /* Fast re-authentication username */
	u16 counter;
	u8 mk[EAP_SIM_MK_LEN];
	u8 k_encr[EAP_SIM_K_ENCR_LEN];
	u8 k_aut[EAP_AKA_PRIME_K_AUT_LEN];
	u8 k_re[EAP_AKA_PRIME_K_RE_LEN];
};

struct eap_sim_reauth *
eap_sim_db_get_reauth_entry(struct eap_sim_db_data *data,
			    const char *reauth_id);

void eap_sim_db_remove_reauth(struct eap_sim_db_data *data,
			      struct eap_sim_reauth *reauth);

int eap_sim_db_get_aka_auth(struct eap_sim_db_data *data, const char *username,
			    u8 *_rand, u8 *autn, u8 *ik, u8 *ck,
			    u8 *res, size_t *res_len, void *cb_session_ctx);

int eap_sim_db_resynchronize(struct eap_sim_db_data *data,
			     const char *username, const u8 *auts,
			     const u8 *_rand);

char * sim_get_username(const u8 *identity, size_t identity_len);

#endif /* EAP_SIM_DB_H */
