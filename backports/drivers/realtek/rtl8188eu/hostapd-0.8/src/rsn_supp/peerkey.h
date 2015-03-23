/*
 * WPA Supplicant - PeerKey for Direct Link Setup (DLS)
 * Copyright (c) 2006-2008, Jouni Malinen <j@w1.fi>
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

#ifndef PEERKEY_H
#define PEERKEY_H

#define PEERKEY_MAX_IE_LEN 80
struct wpa_peerkey {
	struct wpa_peerkey *next;
	int initiator; /* whether this end was initator for SMK handshake */
	u8 addr[ETH_ALEN]; /* other end MAC address */
	u8 inonce[WPA_NONCE_LEN]; /* Initiator Nonce */
	u8 pnonce[WPA_NONCE_LEN]; /* Peer Nonce */
	u8 rsnie_i[PEERKEY_MAX_IE_LEN]; /* Initiator RSN IE */
	size_t rsnie_i_len;
	u8 rsnie_p[PEERKEY_MAX_IE_LEN]; /* Peer RSN IE */
	size_t rsnie_p_len;
	u8 smk[PMK_LEN];
	int smk_complete;
	u8 smkid[PMKID_LEN];
	u32 lifetime;
	os_time_t expiration;
	int cipher; /* Selected cipher (WPA_CIPHER_*) */
	u8 replay_counter[WPA_REPLAY_COUNTER_LEN];
	int replay_counter_set;
	int use_sha256; /* whether AKMP indicate SHA256-based derivations */

	struct wpa_ptk stk, tstk;
	int stk_set, tstk_set;
};


#ifdef CONFIG_PEERKEY

int peerkey_verify_eapol_key_mic(struct wpa_sm *sm,
				 struct wpa_peerkey *peerkey,
				 struct wpa_eapol_key *key, u16 ver,
				 const u8 *buf, size_t len);
void peerkey_rx_eapol_4way(struct wpa_sm *sm, struct wpa_peerkey *peerkey,
			   struct wpa_eapol_key *key, u16 key_info, u16 ver);
void peerkey_rx_eapol_smk(struct wpa_sm *sm, const u8 *src_addr,
			  struct wpa_eapol_key *key, size_t extra_len,
			  u16 key_info, u16 ver);
void peerkey_deinit(struct wpa_sm *sm);

#else /* CONFIG_PEERKEY */

static inline int
peerkey_verify_eapol_key_mic(struct wpa_sm *sm,
			     struct wpa_peerkey *peerkey,
			     struct wpa_eapol_key *key, u16 ver,
			     const u8 *buf, size_t len)
{
	return -1;
}

static inline void
peerkey_rx_eapol_4way(struct wpa_sm *sm, struct wpa_peerkey *peerkey,
		      struct wpa_eapol_key *key, u16 key_info, u16 ver)
{
}

static inline void
peerkey_rx_eapol_smk(struct wpa_sm *sm, const u8 *src_addr,
		     struct wpa_eapol_key *key, size_t extra_len,
		     u16 key_info, u16 ver)
{
}

static inline void peerkey_deinit(struct wpa_sm *sm)
{
}

#endif /* CONFIG_PEERKEY */

#endif /* PEERKEY_H */
