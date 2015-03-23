/*
 * hostapd / EAP-SIM database/authenticator gateway
 * Copyright (c) 2005-2007, Jouni Malinen <j@w1.fi>
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

void * eap_sim_db_init(const char *config,
		       void (*get_complete_cb)(void *ctx, void *session_ctx),
		       void *ctx);

void eap_sim_db_deinit(void *priv);

int eap_sim_db_get_gsm_triplets(void *priv, const u8 *identity,
				size_t identity_len, int max_chal,
				u8 *_rand, u8 *kc, u8 *sres,
				void *cb_session_ctx);

#define EAP_SIM_DB_FAILURE -1
#define EAP_SIM_DB_PENDING -2

int eap_sim_db_identity_known(void *priv, const u8 *identity,
			      size_t identity_len);

char * eap_sim_db_get_next_pseudonym(void *priv, int aka);

char * eap_sim_db_get_next_reauth_id(void *priv, int aka);

int eap_sim_db_add_pseudonym(void *priv, const u8 *identity,
			     size_t identity_len, char *pseudonym);

int eap_sim_db_add_reauth(void *priv, const u8 *identity,
			  size_t identity_len, char *reauth_id, u16 counter,
			  const u8 *mk);
int eap_sim_db_add_reauth_prime(void *priv, const u8 *identity,
				size_t identity_len, char *reauth_id,
				u16 counter, const u8 *k_encr, const u8 *k_aut,
				const u8 *k_re);

const u8 * eap_sim_db_get_permanent(void *priv, const u8 *identity,
				    size_t identity_len, size_t *len);

struct eap_sim_reauth {
	struct eap_sim_reauth *next;
	u8 *identity;
	size_t identity_len;
	char *reauth_id;
	u16 counter;
	int aka_prime;
	u8 mk[EAP_SIM_MK_LEN];
	u8 k_encr[EAP_SIM_K_ENCR_LEN];
	u8 k_aut[EAP_AKA_PRIME_K_AUT_LEN];
	u8 k_re[EAP_AKA_PRIME_K_RE_LEN];
};

struct eap_sim_reauth *
eap_sim_db_get_reauth_entry(void *priv, const u8 *identity,
			    size_t identity_len);

void eap_sim_db_remove_reauth(void *priv, struct eap_sim_reauth *reauth);

int eap_sim_db_get_aka_auth(void *priv, const u8 *identity,
			    size_t identity_len, u8 *_rand, u8 *autn, u8 *ik,
			    u8 *ck, u8 *res, size_t *res_len,
			    void *cb_session_ctx);

int eap_sim_db_resynchronize(void *priv, const u8 *identity,
			     size_t identity_len, const u8 *auts,
			     const u8 *_rand);

#endif /* EAP_SIM_DB_H */
