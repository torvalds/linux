/*
 * hostapd - PeerKey for Direct Link Setup (DLS)
 * Copyright (c) 2006-2009, Jouni Malinen <j@w1.fi>
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

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "crypto/random.h"
#include "wpa_auth.h"
#include "wpa_auth_i.h"
#include "wpa_auth_ie.h"

#ifdef CONFIG_PEERKEY

static void wpa_stsl_step(void *eloop_ctx, void *timeout_ctx)
{
#if 0
	struct wpa_authenticator *wpa_auth = eloop_ctx;
	struct wpa_stsl_negotiation *neg = timeout_ctx;
#endif

	/* TODO: ? */
}


struct wpa_stsl_search {
	const u8 *addr;
	struct wpa_state_machine *sm;
};


static int wpa_stsl_select_sta(struct wpa_state_machine *sm, void *ctx)
{
	struct wpa_stsl_search *search = ctx;
	if (os_memcmp(search->addr, sm->addr, ETH_ALEN) == 0) {
		search->sm = sm;
		return 1;
	}
	return 0;
}


static void wpa_smk_send_error(struct wpa_authenticator *wpa_auth,
			       struct wpa_state_machine *sm, const u8 *peer,
			       u16 mui, u16 error_type)
{
	u8 kde[2 + RSN_SELECTOR_LEN + ETH_ALEN +
	       2 + RSN_SELECTOR_LEN + sizeof(struct rsn_error_kde)];
	u8 *pos;
	struct rsn_error_kde error;

	wpa_auth_logger(wpa_auth, sm->addr, LOGGER_DEBUG,
			"Sending SMK Error");

	pos = kde;

	if (peer) {
		pos = wpa_add_kde(pos, RSN_KEY_DATA_MAC_ADDR, peer, ETH_ALEN,
				  NULL, 0);
	}

	error.mui = host_to_be16(mui);
	error.error_type = host_to_be16(error_type);
	pos = wpa_add_kde(pos, RSN_KEY_DATA_ERROR,
			  (u8 *) &error, sizeof(error), NULL, 0);

	__wpa_send_eapol(wpa_auth, sm,
			 WPA_KEY_INFO_SECURE | WPA_KEY_INFO_MIC |
			 WPA_KEY_INFO_SMK_MESSAGE | WPA_KEY_INFO_ERROR,
			 NULL, NULL, kde, pos - kde, 0, 0, 0);
}


void wpa_smk_m1(struct wpa_authenticator *wpa_auth,
		struct wpa_state_machine *sm, struct wpa_eapol_key *key)
{
	struct wpa_eapol_ie_parse kde;
	struct wpa_stsl_search search;
	u8 *buf, *pos;
	size_t buf_len;

	if (wpa_parse_kde_ies((const u8 *) (key + 1),
			      WPA_GET_BE16(key->key_data_length), &kde) < 0) {
		wpa_printf(MSG_INFO, "RSN: Failed to parse KDEs in SMK M1");
		return;
	}

	if (kde.rsn_ie == NULL || kde.mac_addr == NULL ||
	    kde.mac_addr_len < ETH_ALEN) {
		wpa_printf(MSG_INFO, "RSN: No RSN IE or MAC address KDE in "
			   "SMK M1");
		return;
	}

	/* Initiator = sm->addr; Peer = kde.mac_addr */

	search.addr = kde.mac_addr;
	search.sm = NULL;
	if (wpa_auth_for_each_sta(wpa_auth, wpa_stsl_select_sta, &search) ==
	    0 || search.sm == NULL) {
		wpa_printf(MSG_DEBUG, "RSN: SMK handshake with " MACSTR
			   " aborted - STA not associated anymore",
			   MAC2STR(kde.mac_addr));
		wpa_smk_send_error(wpa_auth, sm, kde.mac_addr, STK_MUI_SMK,
				   STK_ERR_STA_NR);
		/* FIX: wpa_stsl_remove(wpa_auth, neg); */
		return;
	}

	buf_len = kde.rsn_ie_len + 2 + RSN_SELECTOR_LEN + ETH_ALEN;
	buf = os_malloc(buf_len);
	if (buf == NULL)
		return;
	/* Initiator RSN IE */
	os_memcpy(buf, kde.rsn_ie, kde.rsn_ie_len);
	pos = buf + kde.rsn_ie_len;
	/* Initiator MAC Address */
	pos = wpa_add_kde(pos, RSN_KEY_DATA_MAC_ADDR, sm->addr, ETH_ALEN,
			  NULL, 0);

	/* SMK M2:
	 * EAPOL-Key(S=1, M=1, A=1, I=0, K=0, SM=1, KeyRSC=0, Nonce=INonce,
	 *           MIC=MIC, DataKDs=(RSNIE_I, MAC_I KDE)
	 */

	wpa_auth_logger(wpa_auth, search.sm->addr, LOGGER_DEBUG,
			"Sending SMK M2");

	__wpa_send_eapol(wpa_auth, search.sm,
			 WPA_KEY_INFO_SECURE | WPA_KEY_INFO_MIC |
			 WPA_KEY_INFO_ACK | WPA_KEY_INFO_SMK_MESSAGE,
			 NULL, key->key_nonce, buf, pos - buf, 0, 0, 0);

	os_free(buf);
}


static void wpa_send_smk_m4(struct wpa_authenticator *wpa_auth,
			    struct wpa_state_machine *sm,
			    struct wpa_eapol_key *key,
			    struct wpa_eapol_ie_parse *kde,
			    const u8 *smk)
{
	u8 *buf, *pos;
	size_t buf_len;
	u32 lifetime;

	/* SMK M4:
	 * EAPOL-Key(S=1, M=1, A=0, I=1, K=0, SM=1, KeyRSC=0, Nonce=PNonce,
	 *           MIC=MIC, DataKDs=(MAC_I KDE, INonce KDE, SMK KDE,
	 *           Lifetime KDE)
	 */

	buf_len = 2 + RSN_SELECTOR_LEN + ETH_ALEN +
		2 + RSN_SELECTOR_LEN + WPA_NONCE_LEN +
		2 + RSN_SELECTOR_LEN + PMK_LEN + WPA_NONCE_LEN +
		2 + RSN_SELECTOR_LEN + sizeof(lifetime);
	pos = buf = os_malloc(buf_len);
	if (buf == NULL)
		return;

	/* Initiator MAC Address */
	pos = wpa_add_kde(pos, RSN_KEY_DATA_MAC_ADDR, kde->mac_addr, ETH_ALEN,
			  NULL, 0);

	/* Initiator Nonce */
	pos = wpa_add_kde(pos, RSN_KEY_DATA_NONCE, kde->nonce, WPA_NONCE_LEN,
			  NULL, 0);

	/* SMK with PNonce */
	pos = wpa_add_kde(pos, RSN_KEY_DATA_SMK, smk, PMK_LEN,
			  key->key_nonce, WPA_NONCE_LEN);

	/* Lifetime */
	lifetime = htonl(43200); /* dot11RSNAConfigSMKLifetime */
	pos = wpa_add_kde(pos, RSN_KEY_DATA_LIFETIME,
			  (u8 *) &lifetime, sizeof(lifetime), NULL, 0);

	wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
			"Sending SMK M4");

	__wpa_send_eapol(wpa_auth, sm,
			 WPA_KEY_INFO_SECURE | WPA_KEY_INFO_MIC |
			 WPA_KEY_INFO_INSTALL | WPA_KEY_INFO_SMK_MESSAGE,
			 NULL, key->key_nonce, buf, pos - buf, 0, 1, 0);

	os_free(buf);
}


static void wpa_send_smk_m5(struct wpa_authenticator *wpa_auth,
			    struct wpa_state_machine *sm,
			    struct wpa_eapol_key *key,
			    struct wpa_eapol_ie_parse *kde,
			    const u8 *smk, const u8 *peer)
{
	u8 *buf, *pos;
	size_t buf_len;
	u32 lifetime;

	/* SMK M5:
	 * EAPOL-Key(S=1, M=1, A=0, I=0, K=0, SM=1, KeyRSC=0, Nonce=INonce,
	 *           MIC=MIC, DataKDs=(RSNIE_P, MAC_P KDE, PNonce, SMK KDE,
	 *                             Lifetime KDE))
	 */

	buf_len = kde->rsn_ie_len +
		2 + RSN_SELECTOR_LEN + ETH_ALEN +
		2 + RSN_SELECTOR_LEN + WPA_NONCE_LEN +
		2 + RSN_SELECTOR_LEN + PMK_LEN + WPA_NONCE_LEN +
		2 + RSN_SELECTOR_LEN + sizeof(lifetime);
	pos = buf = os_malloc(buf_len);
	if (buf == NULL)
		return;

	/* Peer RSN IE */
	os_memcpy(buf, kde->rsn_ie, kde->rsn_ie_len);
	pos = buf + kde->rsn_ie_len;

	/* Peer MAC Address */
	pos = wpa_add_kde(pos, RSN_KEY_DATA_MAC_ADDR, peer, ETH_ALEN, NULL, 0);

	/* PNonce */
	pos = wpa_add_kde(pos, RSN_KEY_DATA_NONCE, key->key_nonce,
			  WPA_NONCE_LEN, NULL, 0);

	/* SMK and INonce */
	pos = wpa_add_kde(pos, RSN_KEY_DATA_SMK, smk, PMK_LEN,
			  kde->nonce, WPA_NONCE_LEN);

	/* Lifetime */
	lifetime = htonl(43200); /* dot11RSNAConfigSMKLifetime */
	pos = wpa_add_kde(pos, RSN_KEY_DATA_LIFETIME,
			  (u8 *) &lifetime, sizeof(lifetime), NULL, 0);

	wpa_auth_logger(sm->wpa_auth, sm->addr, LOGGER_DEBUG,
			"Sending SMK M5");

	__wpa_send_eapol(wpa_auth, sm,
			 WPA_KEY_INFO_SECURE | WPA_KEY_INFO_MIC |
			 WPA_KEY_INFO_SMK_MESSAGE,
			 NULL, kde->nonce, buf, pos - buf, 0, 1, 0);

	os_free(buf);
}


void wpa_smk_m3(struct wpa_authenticator *wpa_auth,
		struct wpa_state_machine *sm, struct wpa_eapol_key *key)
{
	struct wpa_eapol_ie_parse kde;
	struct wpa_stsl_search search;
	u8 smk[32], buf[ETH_ALEN + 8 + 2 * WPA_NONCE_LEN], *pos;

	if (wpa_parse_kde_ies((const u8 *) (key + 1),
			      WPA_GET_BE16(key->key_data_length), &kde) < 0) {
		wpa_printf(MSG_INFO, "RSN: Failed to parse KDEs in SMK M3");
		return;
	}

	if (kde.rsn_ie == NULL ||
	    kde.mac_addr == NULL || kde.mac_addr_len < ETH_ALEN ||
	    kde.nonce == NULL || kde.nonce_len < WPA_NONCE_LEN) {
		wpa_printf(MSG_INFO, "RSN: No RSN IE, MAC address KDE, or "
			   "Nonce KDE in SMK M3");
		return;
	}

	/* Peer = sm->addr; Initiator = kde.mac_addr;
	 * Peer Nonce = key->key_nonce; Initiator Nonce = kde.nonce */

	search.addr = kde.mac_addr;
	search.sm = NULL;
	if (wpa_auth_for_each_sta(wpa_auth, wpa_stsl_select_sta, &search) ==
	    0 || search.sm == NULL) {
		wpa_printf(MSG_DEBUG, "RSN: SMK handshake with " MACSTR
			   " aborted - STA not associated anymore",
			   MAC2STR(kde.mac_addr));
		wpa_smk_send_error(wpa_auth, sm, kde.mac_addr, STK_MUI_SMK,
				   STK_ERR_STA_NR);
		/* FIX: wpa_stsl_remove(wpa_auth, neg); */
		return;
	}

	if (random_get_bytes(smk, PMK_LEN)) {
		wpa_printf(MSG_DEBUG, "RSN: Failed to generate SMK");
		return;
	}

	/* SMK = PRF-256(Random number, "SMK Derivation",
	 *               AA || Time || INonce || PNonce)
	 */
	os_memcpy(buf, wpa_auth->addr, ETH_ALEN);
	pos = buf + ETH_ALEN;
	wpa_get_ntp_timestamp(pos);
	pos += 8;
	os_memcpy(pos, kde.nonce, WPA_NONCE_LEN);
	pos += WPA_NONCE_LEN;
	os_memcpy(pos, key->key_nonce, WPA_NONCE_LEN);
#ifdef CONFIG_IEEE80211W
	sha256_prf(smk, PMK_LEN, "SMK Derivation", buf, sizeof(buf),
		   smk, PMK_LEN);
#else /* CONFIG_IEEE80211W */
	sha1_prf(smk, PMK_LEN, "SMK Derivation", buf, sizeof(buf),
		 smk, PMK_LEN);
#endif /* CONFIG_IEEE80211W */

	wpa_hexdump_key(MSG_DEBUG, "RSN: SMK", smk, PMK_LEN);

	wpa_send_smk_m4(wpa_auth, sm, key, &kde, smk);
	wpa_send_smk_m5(wpa_auth, search.sm, key, &kde, smk, sm->addr);

	/* Authenticator does not need SMK anymore and it is required to forget
	 * it. */
	os_memset(smk, 0, sizeof(*smk));
}


void wpa_smk_error(struct wpa_authenticator *wpa_auth,
		   struct wpa_state_machine *sm, struct wpa_eapol_key *key)
{
	struct wpa_eapol_ie_parse kde;
	struct wpa_stsl_search search;
	struct rsn_error_kde error;
	u16 mui, error_type;

	if (wpa_parse_kde_ies((const u8 *) (key + 1),
			      WPA_GET_BE16(key->key_data_length), &kde) < 0) {
		wpa_printf(MSG_INFO, "RSN: Failed to parse KDEs in SMK Error");
		return;
	}

	if (kde.mac_addr == NULL || kde.mac_addr_len < ETH_ALEN ||
	    kde.error == NULL || kde.error_len < sizeof(error)) {
		wpa_printf(MSG_INFO, "RSN: No MAC address or Error KDE in "
			   "SMK Error");
		return;
	}

	search.addr = kde.mac_addr;
	search.sm = NULL;
	if (wpa_auth_for_each_sta(wpa_auth, wpa_stsl_select_sta, &search) ==
	    0 || search.sm == NULL) {
		wpa_printf(MSG_DEBUG, "RSN: Peer STA " MACSTR " not "
			   "associated for SMK Error message from " MACSTR,
			   MAC2STR(kde.mac_addr), MAC2STR(sm->addr));
		return;
	}

	os_memcpy(&error, kde.error, sizeof(error));
	mui = be_to_host16(error.mui);
	error_type = be_to_host16(error.error_type);
	wpa_auth_vlogger(wpa_auth, sm->addr, LOGGER_INFO,
			 "STA reported SMK Error: Peer " MACSTR
			 " MUI %d Error Type %d",
			 MAC2STR(kde.mac_addr), mui, error_type);

	wpa_smk_send_error(wpa_auth, search.sm, sm->addr, mui, error_type);
}


int wpa_stsl_remove(struct wpa_authenticator *wpa_auth,
		    struct wpa_stsl_negotiation *neg)
{
	struct wpa_stsl_negotiation *pos, *prev;

	if (wpa_auth == NULL)
		return -1;
	pos = wpa_auth->stsl_negotiations;
	prev = NULL;
	while (pos) {
		if (pos == neg) {
			if (prev)
				prev->next = pos->next;
			else
				wpa_auth->stsl_negotiations = pos->next;

			eloop_cancel_timeout(wpa_stsl_step, wpa_auth, pos);
			os_free(pos);
			return 0;
		}
		prev = pos;
		pos = pos->next;
	}

	return -1;
}

#endif /* CONFIG_PEERKEY */
