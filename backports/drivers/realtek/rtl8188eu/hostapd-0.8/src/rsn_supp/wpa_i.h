/*
 * Internal WPA/RSN supplicant state machine definitions
 * Copyright (c) 2004-2010, Jouni Malinen <j@w1.fi>
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

#ifndef WPA_I_H
#define WPA_I_H

#include "utils/list.h"

struct wpa_peerkey;
struct wpa_tdls_peer;
struct wpa_eapol_key;

/**
 * struct wpa_sm - Internal WPA state machine data
 */
struct wpa_sm {
	u8 pmk[PMK_LEN];
	size_t pmk_len;
	struct wpa_ptk ptk, tptk;
	int ptk_set, tptk_set;
	u8 snonce[WPA_NONCE_LEN];
	u8 anonce[WPA_NONCE_LEN]; /* ANonce from the last 1/4 msg */
	int renew_snonce;
	u8 rx_replay_counter[WPA_REPLAY_COUNTER_LEN];
	int rx_replay_counter_set;
	u8 request_counter[WPA_REPLAY_COUNTER_LEN];

	struct eapol_sm *eapol; /* EAPOL state machine from upper level code */

	struct rsn_pmksa_cache *pmksa; /* PMKSA cache */
	struct rsn_pmksa_cache_entry *cur_pmksa; /* current PMKSA entry */
	struct dl_list pmksa_candidates;

	struct l2_packet_data *l2_preauth;
	struct l2_packet_data *l2_preauth_br;
	struct l2_packet_data *l2_tdls;
	u8 preauth_bssid[ETH_ALEN]; /* current RSN pre-auth peer or
				     * 00:00:00:00:00:00 if no pre-auth is
				     * in progress */
	struct eapol_sm *preauth_eapol;

	struct wpa_sm_ctx *ctx;

	void *scard_ctx; /* context for smartcard callbacks */
	int fast_reauth; /* whether EAP fast re-authentication is enabled */

	void *network_ctx;
	int peerkey_enabled;
	int allowed_pairwise_cipher; /* bitfield of WPA_CIPHER_* */
	int proactive_key_caching;
	int eap_workaround;
	void *eap_conf_ctx;
	u8 ssid[32];
	size_t ssid_len;
	int wpa_ptk_rekey;

	u8 own_addr[ETH_ALEN];
	const char *ifname;
	const char *bridge_ifname;
	u8 bssid[ETH_ALEN];

	unsigned int dot11RSNAConfigPMKLifetime;
	unsigned int dot11RSNAConfigPMKReauthThreshold;
	unsigned int dot11RSNAConfigSATimeout;

	unsigned int dot11RSNA4WayHandshakeFailures;

	/* Selected configuration (based on Beacon/ProbeResp WPA IE) */
	unsigned int proto;
	unsigned int pairwise_cipher;
	unsigned int group_cipher;
	unsigned int key_mgmt;
	unsigned int mgmt_group_cipher;

	int rsn_enabled; /* Whether RSN is enabled in configuration */
	int mfp; /* 0 = disabled, 1 = optional, 2 = mandatory */

	u8 *assoc_wpa_ie; /* Own WPA/RSN IE from (Re)AssocReq */
	size_t assoc_wpa_ie_len;
	u8 *ap_wpa_ie, *ap_rsn_ie;
	size_t ap_wpa_ie_len, ap_rsn_ie_len;

#ifdef CONFIG_PEERKEY
	struct wpa_peerkey *peerkey;
#endif /* CONFIG_PEERKEY */
#ifdef CONFIG_TDLS
	struct wpa_tdls_peer *tdls;
	int tdls_prohibited;
	int tdls_disabled;
#endif /* CONFIG_TDLS */

#ifdef CONFIG_IEEE80211R
	u8 xxkey[PMK_LEN]; /* PSK or the second 256 bits of MSK */
	size_t xxkey_len;
	u8 pmk_r0[PMK_LEN];
	u8 pmk_r0_name[WPA_PMK_NAME_LEN];
	u8 pmk_r1[PMK_LEN];
	u8 pmk_r1_name[WPA_PMK_NAME_LEN];
	u8 mobility_domain[MOBILITY_DOMAIN_ID_LEN];
	u8 r0kh_id[FT_R0KH_ID_MAX_LEN];
	size_t r0kh_id_len;
	u8 r1kh_id[FT_R1KH_ID_LEN];
	int ft_completed;
	int over_the_ds_in_progress;
	u8 target_ap[ETH_ALEN]; /* over-the-DS target AP */
	int set_ptk_after_assoc;
	u8 mdie_ft_capab; /* FT Capability and Policy from target AP MDIE */
	u8 *assoc_resp_ies; /* MDIE and FTIE from (Re)Association Response */
	size_t assoc_resp_ies_len;
#endif /* CONFIG_IEEE80211R */
};


static inline void wpa_sm_set_state(struct wpa_sm *sm, enum wpa_states state)
{
	WPA_ASSERT(sm->ctx->set_state);
	sm->ctx->set_state(sm->ctx->ctx, state);
}

static inline enum wpa_states wpa_sm_get_state(struct wpa_sm *sm)
{
	WPA_ASSERT(sm->ctx->get_state);
	return sm->ctx->get_state(sm->ctx->ctx);
}

static inline void wpa_sm_deauthenticate(struct wpa_sm *sm, int reason_code)
{
	WPA_ASSERT(sm->ctx->deauthenticate);
	sm->ctx->deauthenticate(sm->ctx->ctx, reason_code);
}

static inline void wpa_sm_disassociate(struct wpa_sm *sm, int reason_code)
{
	WPA_ASSERT(sm->ctx->disassociate);
	sm->ctx->disassociate(sm->ctx->ctx, reason_code);
}

static inline int wpa_sm_set_key(struct wpa_sm *sm, enum wpa_alg alg,
				 const u8 *addr, int key_idx, int set_tx,
				 const u8 *seq, size_t seq_len,
				 const u8 *key, size_t key_len)
{
	WPA_ASSERT(sm->ctx->set_key);
	return sm->ctx->set_key(sm->ctx->ctx, alg, addr, key_idx, set_tx,
				seq, seq_len, key, key_len);
}

static inline void * wpa_sm_get_network_ctx(struct wpa_sm *sm)
{
	WPA_ASSERT(sm->ctx->get_network_ctx);
	return sm->ctx->get_network_ctx(sm->ctx->ctx);
}

static inline int wpa_sm_get_bssid(struct wpa_sm *sm, u8 *bssid)
{
	WPA_ASSERT(sm->ctx->get_bssid);
	return sm->ctx->get_bssid(sm->ctx->ctx, bssid);
}

static inline int wpa_sm_ether_send(struct wpa_sm *sm, const u8 *dest,
				    u16 proto, const u8 *buf, size_t len)
{
	WPA_ASSERT(sm->ctx->ether_send);
	return sm->ctx->ether_send(sm->ctx->ctx, dest, proto, buf, len);
}

static inline int wpa_sm_get_beacon_ie(struct wpa_sm *sm)
{
	WPA_ASSERT(sm->ctx->get_beacon_ie);
	return sm->ctx->get_beacon_ie(sm->ctx->ctx);
}

static inline void wpa_sm_cancel_auth_timeout(struct wpa_sm *sm)
{
	WPA_ASSERT(sm->ctx->cancel_auth_timeout);
	sm->ctx->cancel_auth_timeout(sm->ctx->ctx);
}

static inline u8 * wpa_sm_alloc_eapol(struct wpa_sm *sm, u8 type,
				      const void *data, u16 data_len,
				      size_t *msg_len, void **data_pos)
{
	WPA_ASSERT(sm->ctx->alloc_eapol);
	return sm->ctx->alloc_eapol(sm->ctx->ctx, type, data, data_len,
				    msg_len, data_pos);
}

static inline int wpa_sm_add_pmkid(struct wpa_sm *sm, const u8 *bssid,
				   const u8 *pmkid)
{
	WPA_ASSERT(sm->ctx->add_pmkid);
	return sm->ctx->add_pmkid(sm->ctx->ctx, bssid, pmkid);
}

static inline int wpa_sm_remove_pmkid(struct wpa_sm *sm, const u8 *bssid,
				      const u8 *pmkid)
{
	WPA_ASSERT(sm->ctx->remove_pmkid);
	return sm->ctx->remove_pmkid(sm->ctx->ctx, bssid, pmkid);
}

static inline int wpa_sm_mlme_setprotection(struct wpa_sm *sm, const u8 *addr,
					    int protect_type, int key_type)
{
	WPA_ASSERT(sm->ctx->mlme_setprotection);
	return sm->ctx->mlme_setprotection(sm->ctx->ctx, addr, protect_type,
					   key_type);
}

static inline int wpa_sm_update_ft_ies(struct wpa_sm *sm, const u8 *md,
				       const u8 *ies, size_t ies_len)
{
	if (sm->ctx->update_ft_ies)
		return sm->ctx->update_ft_ies(sm->ctx->ctx, md, ies, ies_len);
	return -1;
}

static inline int wpa_sm_send_ft_action(struct wpa_sm *sm, u8 action,
					const u8 *target_ap,
					const u8 *ies, size_t ies_len)
{
	if (sm->ctx->send_ft_action)
		return sm->ctx->send_ft_action(sm->ctx->ctx, action, target_ap,
					       ies, ies_len);
	return -1;
}

static inline int wpa_sm_mark_authenticated(struct wpa_sm *sm,
					    const u8 *target_ap)
{
	if (sm->ctx->mark_authenticated)
		return sm->ctx->mark_authenticated(sm->ctx->ctx, target_ap);
	return -1;
}

#ifdef CONFIG_TDLS
static inline int wpa_sm_send_tdls_mgmt(struct wpa_sm *sm, const u8 *dst,
					u8 action_code, u8 dialog_token,
					u16 status_code, const u8 *buf,
					size_t len)
{
	if (sm->ctx->send_tdls_mgmt)
		return sm->ctx->send_tdls_mgmt(sm->ctx->ctx, dst, action_code,
					       dialog_token, status_code,
					       buf, len);
	return -1;
}

static inline int wpa_sm_tdls_oper(struct wpa_sm *sm, int oper,
				   const u8 *peer)
{
	if (sm->ctx->tdls_oper)
		return sm->ctx->tdls_oper(sm->ctx->ctx, oper, peer);
	return -1;
}
#endif /* CONFIG_TDLS */

void wpa_eapol_key_send(struct wpa_sm *sm, const u8 *kck,
			int ver, const u8 *dest, u16 proto,
			u8 *msg, size_t msg_len, u8 *key_mic);
int wpa_supplicant_send_2_of_4(struct wpa_sm *sm, const unsigned char *dst,
			       const struct wpa_eapol_key *key,
			       int ver, const u8 *nonce,
			       const u8 *wpa_ie, size_t wpa_ie_len,
			       struct wpa_ptk *ptk);
int wpa_supplicant_send_4_of_4(struct wpa_sm *sm, const unsigned char *dst,
			       const struct wpa_eapol_key *key,
			       u16 ver, u16 key_info,
			       const u8 *kde, size_t kde_len,
			       struct wpa_ptk *ptk);

int wpa_derive_ptk_ft(struct wpa_sm *sm, const unsigned char *src_addr,
		      const struct wpa_eapol_key *key,
		      struct wpa_ptk *ptk, size_t ptk_len);

void wpa_tdls_assoc(struct wpa_sm *sm);
void wpa_tdls_disassoc(struct wpa_sm *sm);

#endif /* WPA_I_H */
