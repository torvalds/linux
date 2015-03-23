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

#include "includes.h"

#ifdef CONFIG_PEERKEY

#include "common.h"
#include "eloop.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "crypto/random.h"
#include "common/ieee802_11_defs.h"
#include "wpa.h"
#include "wpa_i.h"
#include "wpa_ie.h"
#include "peerkey.h"


static u8 * wpa_add_ie(u8 *pos, const u8 *ie, size_t ie_len)
{
	os_memcpy(pos, ie, ie_len);
	return pos + ie_len;
}


static u8 * wpa_add_kde(u8 *pos, u32 kde, const u8 *data, size_t data_len)
{
	*pos++ = WLAN_EID_VENDOR_SPECIFIC;
	*pos++ = RSN_SELECTOR_LEN + data_len;
	RSN_SELECTOR_PUT(pos, kde);
	pos += RSN_SELECTOR_LEN;
	os_memcpy(pos, data, data_len);
	pos += data_len;
	return pos;
}


static void wpa_supplicant_smk_timeout(void *eloop_ctx, void *timeout_ctx)
{
#if 0
	struct wpa_sm *sm = eloop_ctx;
	struct wpa_peerkey *peerkey = timeout_ctx;
#endif
	/* TODO: time out SMK and any STK that was generated using this SMK */
}


static void wpa_supplicant_peerkey_free(struct wpa_sm *sm,
					struct wpa_peerkey *peerkey)
{
	eloop_cancel_timeout(wpa_supplicant_smk_timeout, sm, peerkey);
	os_free(peerkey);
}


static int wpa_supplicant_send_smk_error(struct wpa_sm *sm, const u8 *dst,
					 const u8 *peer,
					 u16 mui, u16 error_type, int ver)
{
	size_t rlen;
	struct wpa_eapol_key *err;
	struct rsn_error_kde error;
	u8 *rbuf, *pos;
	size_t kde_len;
	u16 key_info;

	kde_len = 2 + RSN_SELECTOR_LEN + sizeof(error);
	if (peer)
		kde_len += 2 + RSN_SELECTOR_LEN + ETH_ALEN;

	rbuf = wpa_sm_alloc_eapol(sm, IEEE802_1X_TYPE_EAPOL_KEY,
				  NULL, sizeof(*err) + kde_len, &rlen,
				  (void *) &err);
	if (rbuf == NULL)
		return -1;

	err->type = EAPOL_KEY_TYPE_RSN;
	key_info = ver | WPA_KEY_INFO_SMK_MESSAGE | WPA_KEY_INFO_MIC |
		WPA_KEY_INFO_SECURE | WPA_KEY_INFO_ERROR |
		WPA_KEY_INFO_REQUEST;
	WPA_PUT_BE16(err->key_info, key_info);
	WPA_PUT_BE16(err->key_length, 0);
	os_memcpy(err->replay_counter, sm->request_counter,
		  WPA_REPLAY_COUNTER_LEN);
	inc_byte_array(sm->request_counter, WPA_REPLAY_COUNTER_LEN);

	WPA_PUT_BE16(err->key_data_length, (u16) kde_len);
	pos = (u8 *) (err + 1);

	if (peer) {
		/* Peer MAC Address KDE */
		pos = wpa_add_kde(pos, RSN_KEY_DATA_MAC_ADDR, peer, ETH_ALEN);
	}

	/* Error KDE */
	error.mui = host_to_be16(mui);
	error.error_type = host_to_be16(error_type);
	wpa_add_kde(pos, RSN_KEY_DATA_ERROR, (u8 *) &error, sizeof(error));

	if (peer) {
		wpa_printf(MSG_DEBUG, "RSN: Sending EAPOL-Key SMK Error (peer "
			   MACSTR " mui %d error_type %d)",
			   MAC2STR(peer), mui, error_type);
	} else {
		wpa_printf(MSG_DEBUG, "RSN: Sending EAPOL-Key SMK Error "
			   "(mui %d error_type %d)", mui, error_type);
	}

	wpa_eapol_key_send(sm, sm->ptk.kck, ver, dst, ETH_P_EAPOL,
			   rbuf, rlen, err->key_mic);

	return 0;
}


static int wpa_supplicant_send_smk_m3(struct wpa_sm *sm,
				      const unsigned char *src_addr,
				      const struct wpa_eapol_key *key,
				      int ver, struct wpa_peerkey *peerkey)
{
	size_t rlen;
	struct wpa_eapol_key *reply;
	u8 *rbuf, *pos;
	size_t kde_len;
	u16 key_info;

	/* KDEs: Peer RSN IE, Initiator MAC Address, Initiator Nonce */
	kde_len = peerkey->rsnie_p_len +
		2 + RSN_SELECTOR_LEN + ETH_ALEN +
		2 + RSN_SELECTOR_LEN + WPA_NONCE_LEN;

	rbuf = wpa_sm_alloc_eapol(sm, IEEE802_1X_TYPE_EAPOL_KEY,
				  NULL, sizeof(*reply) + kde_len, &rlen,
				  (void *) &reply);
	if (rbuf == NULL)
		return -1;

	reply->type = EAPOL_KEY_TYPE_RSN;
	key_info = ver | WPA_KEY_INFO_SMK_MESSAGE | WPA_KEY_INFO_MIC |
		WPA_KEY_INFO_SECURE;
	WPA_PUT_BE16(reply->key_info, key_info);
	WPA_PUT_BE16(reply->key_length, 0);
	os_memcpy(reply->replay_counter, key->replay_counter,
		  WPA_REPLAY_COUNTER_LEN);

	os_memcpy(reply->key_nonce, peerkey->pnonce, WPA_NONCE_LEN);

	WPA_PUT_BE16(reply->key_data_length, (u16) kde_len);
	pos = (u8 *) (reply + 1);

	/* Peer RSN IE */
	pos = wpa_add_ie(pos, peerkey->rsnie_p, peerkey->rsnie_p_len);

	/* Initiator MAC Address KDE */
	pos = wpa_add_kde(pos, RSN_KEY_DATA_MAC_ADDR, peerkey->addr, ETH_ALEN);

	/* Initiator Nonce */
	wpa_add_kde(pos, RSN_KEY_DATA_NONCE, peerkey->inonce, WPA_NONCE_LEN);

	wpa_printf(MSG_DEBUG, "RSN: Sending EAPOL-Key SMK M3");
	wpa_eapol_key_send(sm, sm->ptk.kck, ver, src_addr, ETH_P_EAPOL,
			   rbuf, rlen, reply->key_mic);

	return 0;
}


static int wpa_supplicant_process_smk_m2(
	struct wpa_sm *sm, const unsigned char *src_addr,
	const struct wpa_eapol_key *key, size_t extra_len, int ver)
{
	struct wpa_peerkey *peerkey;
	struct wpa_eapol_ie_parse kde;
	struct wpa_ie_data ie;
	int cipher;
	struct rsn_ie_hdr *hdr;
	u8 *pos;

	wpa_printf(MSG_DEBUG, "RSN: Received SMK M2");

	if (!sm->peerkey_enabled || sm->proto != WPA_PROTO_RSN) {
		wpa_printf(MSG_INFO, "RSN: SMK handshake not allowed for "
			   "the current network");
		return -1;
	}

	if (wpa_supplicant_parse_ies((const u8 *) (key + 1), extra_len, &kde) <
	    0) {
		wpa_printf(MSG_INFO, "RSN: Failed to parse KDEs in SMK M2");
		return -1;
	}

	if (kde.rsn_ie == NULL || kde.mac_addr == NULL ||
	    kde.mac_addr_len < ETH_ALEN) {
		wpa_printf(MSG_INFO, "RSN: No RSN IE or MAC address KDE in "
			   "SMK M2");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "RSN: SMK M2 - SMK initiator " MACSTR,
		   MAC2STR(kde.mac_addr));

	if (kde.rsn_ie_len > PEERKEY_MAX_IE_LEN) {
		wpa_printf(MSG_INFO, "RSN: Too long Initiator RSN IE in SMK "
			   "M2");
		return -1;
	}

	if (wpa_parse_wpa_ie_rsn(kde.rsn_ie, kde.rsn_ie_len, &ie) < 0) {
		wpa_printf(MSG_INFO, "RSN: Failed to parse RSN IE in SMK M2");
		return -1;
	}

	cipher = ie.pairwise_cipher & sm->allowed_pairwise_cipher;
	if (cipher & WPA_CIPHER_CCMP) {
		wpa_printf(MSG_DEBUG, "RSN: Using CCMP for PeerKey");
		cipher = WPA_CIPHER_CCMP;
	} else if (cipher & WPA_CIPHER_TKIP) {
		wpa_printf(MSG_DEBUG, "RSN: Using TKIP for PeerKey");
		cipher = WPA_CIPHER_TKIP;
	} else {
		wpa_printf(MSG_INFO, "RSN: No acceptable cipher in SMK M2");
		wpa_supplicant_send_smk_error(sm, src_addr, kde.mac_addr,
					      STK_MUI_SMK, STK_ERR_CPHR_NS,
					      ver);
		return -1;
	}

	/* TODO: find existing entry and if found, use that instead of adding
	 * a new one; how to handle the case where both ends initiate at the
	 * same time? */
	peerkey = os_zalloc(sizeof(*peerkey));
	if (peerkey == NULL)
		return -1;
	os_memcpy(peerkey->addr, kde.mac_addr, ETH_ALEN);
	os_memcpy(peerkey->inonce, key->key_nonce, WPA_NONCE_LEN);
	os_memcpy(peerkey->rsnie_i, kde.rsn_ie, kde.rsn_ie_len);
	peerkey->rsnie_i_len = kde.rsn_ie_len;
	peerkey->cipher = cipher;
#ifdef CONFIG_IEEE80211W
	if (ie.key_mgmt & (WPA_KEY_MGMT_IEEE8021X_SHA256 |
			   WPA_KEY_MGMT_PSK_SHA256))
		peerkey->use_sha256 = 1;
#endif /* CONFIG_IEEE80211W */

	if (random_get_bytes(peerkey->pnonce, WPA_NONCE_LEN)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: Failed to get random data for PNonce");
		wpa_supplicant_peerkey_free(sm, peerkey);
		return -1;
	}

	hdr = (struct rsn_ie_hdr *) peerkey->rsnie_p;
	hdr->elem_id = WLAN_EID_RSN;
	WPA_PUT_LE16(hdr->version, RSN_VERSION);
	pos = (u8 *) (hdr + 1);
	/* Group Suite can be anything for SMK RSN IE; receiver will just
	 * ignore it. */
	RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_CCMP);
	pos += RSN_SELECTOR_LEN;
	/* Include only the selected cipher in pairwise cipher suite */
	WPA_PUT_LE16(pos, 1);
	pos += 2;
	if (cipher == WPA_CIPHER_CCMP)
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_CCMP);
	else if (cipher == WPA_CIPHER_TKIP)
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_TKIP);
	pos += RSN_SELECTOR_LEN;

	hdr->len = (pos - peerkey->rsnie_p) - 2;
	peerkey->rsnie_p_len = pos - peerkey->rsnie_p;
	wpa_hexdump(MSG_DEBUG, "WPA: RSN IE for SMK handshake",
		    peerkey->rsnie_p, peerkey->rsnie_p_len);

	wpa_supplicant_send_smk_m3(sm, src_addr, key, ver, peerkey);

	peerkey->next = sm->peerkey;
	sm->peerkey = peerkey;

	return 0;
}


/**
 * rsn_smkid - Derive SMK identifier
 * @smk: Station master key (32 bytes)
 * @pnonce: Peer Nonce
 * @mac_p: Peer MAC address
 * @inonce: Initiator Nonce
 * @mac_i: Initiator MAC address
 * @use_sha256: Whether to use SHA256-based KDF
 *
 * 8.5.1.4 Station to station (STK) key hierarchy
 * SMKID = HMAC-SHA1-128(SMK, "SMK Name" || PNonce || MAC_P || INonce || MAC_I)
 */
static void rsn_smkid(const u8 *smk, const u8 *pnonce, const u8 *mac_p,
		      const u8 *inonce, const u8 *mac_i, u8 *smkid,
		      int use_sha256)
{
	char *title = "SMK Name";
	const u8 *addr[5];
	const size_t len[5] = { 8, WPA_NONCE_LEN, ETH_ALEN, WPA_NONCE_LEN,
				ETH_ALEN };
	unsigned char hash[SHA256_MAC_LEN];

	addr[0] = (u8 *) title;
	addr[1] = pnonce;
	addr[2] = mac_p;
	addr[3] = inonce;
	addr[4] = mac_i;

#ifdef CONFIG_IEEE80211W
	if (use_sha256)
		hmac_sha256_vector(smk, PMK_LEN, 5, addr, len, hash);
	else
#endif /* CONFIG_IEEE80211W */
		hmac_sha1_vector(smk, PMK_LEN, 5, addr, len, hash);
	os_memcpy(smkid, hash, PMKID_LEN);
}


static void wpa_supplicant_send_stk_1_of_4(struct wpa_sm *sm,
					   struct wpa_peerkey *peerkey)
{
	size_t mlen;
	struct wpa_eapol_key *msg;
	u8 *mbuf;
	size_t kde_len;
	u16 key_info, ver;

	kde_len = 2 + RSN_SELECTOR_LEN + PMKID_LEN;

	mbuf = wpa_sm_alloc_eapol(sm, IEEE802_1X_TYPE_EAPOL_KEY, NULL,
				  sizeof(*msg) + kde_len, &mlen,
				  (void *) &msg);
	if (mbuf == NULL)
		return;

	msg->type = EAPOL_KEY_TYPE_RSN;

	if (peerkey->cipher == WPA_CIPHER_CCMP)
		ver = WPA_KEY_INFO_TYPE_HMAC_SHA1_AES;
	else
		ver = WPA_KEY_INFO_TYPE_HMAC_MD5_RC4;

	key_info = ver | WPA_KEY_INFO_KEY_TYPE | WPA_KEY_INFO_ACK;
	WPA_PUT_BE16(msg->key_info, key_info);

	if (peerkey->cipher == WPA_CIPHER_CCMP)
		WPA_PUT_BE16(msg->key_length, 16);
	else
		WPA_PUT_BE16(msg->key_length, 32);

	os_memcpy(msg->replay_counter, peerkey->replay_counter,
		  WPA_REPLAY_COUNTER_LEN);
	inc_byte_array(peerkey->replay_counter, WPA_REPLAY_COUNTER_LEN);

	WPA_PUT_BE16(msg->key_data_length, kde_len);
	wpa_add_kde((u8 *) (msg + 1), RSN_KEY_DATA_PMKID,
		    peerkey->smkid, PMKID_LEN);

	if (random_get_bytes(peerkey->inonce, WPA_NONCE_LEN)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"RSN: Failed to get random data for INonce (STK)");
		os_free(mbuf);
		return;
	}
	wpa_hexdump(MSG_DEBUG, "RSN: INonce for STK 4-Way Handshake",
		    peerkey->inonce, WPA_NONCE_LEN);
	os_memcpy(msg->key_nonce, peerkey->inonce, WPA_NONCE_LEN);

	wpa_printf(MSG_DEBUG, "RSN: Sending EAPOL-Key STK 1/4 to " MACSTR,
		   MAC2STR(peerkey->addr));
	wpa_eapol_key_send(sm, NULL, ver, peerkey->addr, ETH_P_EAPOL,
			   mbuf, mlen, NULL);
}


static void wpa_supplicant_send_stk_3_of_4(struct wpa_sm *sm,
					   struct wpa_peerkey *peerkey)
{
	size_t mlen;
	struct wpa_eapol_key *msg;
	u8 *mbuf, *pos;
	size_t kde_len;
	u16 key_info, ver;
	be32 lifetime;

	kde_len = peerkey->rsnie_i_len +
		2 + RSN_SELECTOR_LEN + sizeof(lifetime);

	mbuf = wpa_sm_alloc_eapol(sm, IEEE802_1X_TYPE_EAPOL_KEY, NULL,
				  sizeof(*msg) + kde_len, &mlen,
				  (void *) &msg);
	if (mbuf == NULL)
		return;

	msg->type = EAPOL_KEY_TYPE_RSN;

	if (peerkey->cipher == WPA_CIPHER_CCMP)
		ver = WPA_KEY_INFO_TYPE_HMAC_SHA1_AES;
	else
		ver = WPA_KEY_INFO_TYPE_HMAC_MD5_RC4;

	key_info = ver | WPA_KEY_INFO_KEY_TYPE | WPA_KEY_INFO_ACK |
		WPA_KEY_INFO_MIC | WPA_KEY_INFO_SECURE;
	WPA_PUT_BE16(msg->key_info, key_info);

	if (peerkey->cipher == WPA_CIPHER_CCMP)
		WPA_PUT_BE16(msg->key_length, 16);
	else
		WPA_PUT_BE16(msg->key_length, 32);

	os_memcpy(msg->replay_counter, peerkey->replay_counter,
		  WPA_REPLAY_COUNTER_LEN);
	inc_byte_array(peerkey->replay_counter, WPA_REPLAY_COUNTER_LEN);

	WPA_PUT_BE16(msg->key_data_length, kde_len);
	pos = (u8 *) (msg + 1);
	pos = wpa_add_ie(pos, peerkey->rsnie_i, peerkey->rsnie_i_len);
	lifetime = host_to_be32(peerkey->lifetime);
	wpa_add_kde(pos, RSN_KEY_DATA_LIFETIME,
		    (u8 *) &lifetime, sizeof(lifetime));

	os_memcpy(msg->key_nonce, peerkey->inonce, WPA_NONCE_LEN);

	wpa_printf(MSG_DEBUG, "RSN: Sending EAPOL-Key STK 3/4 to " MACSTR,
		   MAC2STR(peerkey->addr));
	wpa_eapol_key_send(sm, peerkey->stk.kck, ver, peerkey->addr,
			   ETH_P_EAPOL, mbuf, mlen, msg->key_mic);
}


static int wpa_supplicant_process_smk_m4(struct wpa_peerkey *peerkey,
					 struct wpa_eapol_ie_parse *kde)
{
	wpa_printf(MSG_DEBUG, "RSN: Received SMK M4 (Initiator " MACSTR ")",
		   MAC2STR(kde->mac_addr));

	if (os_memcmp(kde->smk + PMK_LEN, peerkey->pnonce, WPA_NONCE_LEN) != 0)
	{
		wpa_printf(MSG_INFO, "RSN: PNonce in SMK KDE does not "
			   "match with the one used in SMK M3");
		return -1;
	}

	if (os_memcmp(kde->nonce, peerkey->inonce, WPA_NONCE_LEN) != 0) {
		wpa_printf(MSG_INFO, "RSN: INonce in SMK M4 did not "
			   "match with the one received in SMK M2");
		return -1;
	}

	return 0;
}


static int wpa_supplicant_process_smk_m5(struct wpa_sm *sm,
					 const unsigned char *src_addr,
					 const struct wpa_eapol_key *key,
					 int ver,
					 struct wpa_peerkey *peerkey,
					 struct wpa_eapol_ie_parse *kde)
{
	int cipher;
	struct wpa_ie_data ie;

	wpa_printf(MSG_DEBUG, "RSN: Received SMK M5 (Peer " MACSTR ")",
		   MAC2STR(kde->mac_addr));
	if (kde->rsn_ie == NULL || kde->rsn_ie_len > PEERKEY_MAX_IE_LEN ||
	    wpa_parse_wpa_ie_rsn(kde->rsn_ie, kde->rsn_ie_len, &ie) < 0) {
		wpa_printf(MSG_INFO, "RSN: No RSN IE in SMK M5");
		/* TODO: abort negotiation */
		return -1;
	}

	if (os_memcmp(key->key_nonce, peerkey->inonce, WPA_NONCE_LEN) != 0) {
		wpa_printf(MSG_INFO, "RSN: Key Nonce in SMK M5 does "
			   "not match with INonce used in SMK M1");
		return -1;
	}

	if (os_memcmp(kde->smk + PMK_LEN, peerkey->inonce, WPA_NONCE_LEN) != 0)
	{
		wpa_printf(MSG_INFO, "RSN: INonce in SMK KDE does not "
			   "match with the one used in SMK M1");
		return -1;
	}

	os_memcpy(peerkey->rsnie_p, kde->rsn_ie, kde->rsn_ie_len);
	peerkey->rsnie_p_len = kde->rsn_ie_len;
	os_memcpy(peerkey->pnonce, kde->nonce, WPA_NONCE_LEN);

	cipher = ie.pairwise_cipher & sm->allowed_pairwise_cipher;
	if (cipher & WPA_CIPHER_CCMP) {
		wpa_printf(MSG_DEBUG, "RSN: Using CCMP for PeerKey");
		peerkey->cipher = WPA_CIPHER_CCMP;
	} else if (cipher & WPA_CIPHER_TKIP) {
		wpa_printf(MSG_DEBUG, "RSN: Using TKIP for PeerKey");
		peerkey->cipher = WPA_CIPHER_TKIP;
	} else {
		wpa_printf(MSG_INFO, "RSN: SMK Peer STA " MACSTR " selected "
			   "unacceptable cipher", MAC2STR(kde->mac_addr));
		wpa_supplicant_send_smk_error(sm, src_addr, kde->mac_addr,
					      STK_MUI_SMK, STK_ERR_CPHR_NS,
					      ver);
		/* TODO: abort negotiation */
		return -1;
	}

	return 0;
}


static int wpa_supplicant_process_smk_m45(
	struct wpa_sm *sm, const unsigned char *src_addr,
	const struct wpa_eapol_key *key, size_t extra_len, int ver)
{
	struct wpa_peerkey *peerkey;
	struct wpa_eapol_ie_parse kde;
	u32 lifetime;
	struct os_time now;

	if (!sm->peerkey_enabled || sm->proto != WPA_PROTO_RSN) {
		wpa_printf(MSG_DEBUG, "RSN: SMK handshake not allowed for "
			   "the current network");
		return -1;
	}

	if (wpa_supplicant_parse_ies((const u8 *) (key + 1), extra_len, &kde) <
	    0) {
		wpa_printf(MSG_INFO, "RSN: Failed to parse KDEs in SMK M4/M5");
		return -1;
	}

	if (kde.mac_addr == NULL || kde.mac_addr_len < ETH_ALEN ||
	    kde.nonce == NULL || kde.nonce_len < WPA_NONCE_LEN ||
	    kde.smk == NULL || kde.smk_len < PMK_LEN + WPA_NONCE_LEN ||
	    kde.lifetime == NULL || kde.lifetime_len < 4) {
		wpa_printf(MSG_INFO, "RSN: No MAC Address, Nonce, SMK, or "
			   "Lifetime KDE in SMK M4/M5");
		return -1;
	}

	for (peerkey = sm->peerkey; peerkey; peerkey = peerkey->next) {
		if (os_memcmp(peerkey->addr, kde.mac_addr, ETH_ALEN) == 0 &&
		    os_memcmp(peerkey->initiator ? peerkey->inonce :
			   peerkey->pnonce,
			   key->key_nonce, WPA_NONCE_LEN) == 0)
			break;
	}
	if (peerkey == NULL) {
		wpa_printf(MSG_INFO, "RSN: No matching SMK handshake found "
			   "for SMK M4/M5: peer " MACSTR,
			   MAC2STR(kde.mac_addr));
		return -1;
	}

	if (peerkey->initiator) {
		if (wpa_supplicant_process_smk_m5(sm, src_addr, key, ver,
						  peerkey, &kde) < 0)
			return -1;
	} else {
		if (wpa_supplicant_process_smk_m4(peerkey, &kde) < 0)
			return -1;
	}

	os_memcpy(peerkey->smk, kde.smk, PMK_LEN);
	peerkey->smk_complete = 1;
	wpa_hexdump_key(MSG_DEBUG, "RSN: SMK", peerkey->smk, PMK_LEN);
	lifetime = WPA_GET_BE32(kde.lifetime);
	wpa_printf(MSG_DEBUG, "RSN: SMK lifetime %u seconds", lifetime);
	if (lifetime > 1000000000)
		lifetime = 1000000000; /* avoid overflowing expiration time */
	peerkey->lifetime = lifetime;
	os_get_time(&now);
	peerkey->expiration = now.sec + lifetime;
	eloop_register_timeout(lifetime, 0, wpa_supplicant_smk_timeout,
			       sm, peerkey);

	if (peerkey->initiator) {
		rsn_smkid(peerkey->smk, peerkey->pnonce, peerkey->addr,
			  peerkey->inonce, sm->own_addr, peerkey->smkid,
			  peerkey->use_sha256);
		wpa_supplicant_send_stk_1_of_4(sm, peerkey);
	} else {
		rsn_smkid(peerkey->smk, peerkey->pnonce, sm->own_addr,
			  peerkey->inonce, peerkey->addr, peerkey->smkid,
			  peerkey->use_sha256);
	}
	wpa_hexdump(MSG_DEBUG, "RSN: SMKID", peerkey->smkid, PMKID_LEN);

	return 0;
}


static int wpa_supplicant_process_smk_error(
	struct wpa_sm *sm, const unsigned char *src_addr,
	const struct wpa_eapol_key *key, size_t extra_len)
{
	struct wpa_eapol_ie_parse kde;
	struct rsn_error_kde error;
	u8 peer[ETH_ALEN];
	u16 error_type;

	wpa_printf(MSG_DEBUG, "RSN: Received SMK Error");

	if (!sm->peerkey_enabled || sm->proto != WPA_PROTO_RSN) {
		wpa_printf(MSG_DEBUG, "RSN: SMK handshake not allowed for "
			   "the current network");
		return -1;
	}

	if (wpa_supplicant_parse_ies((const u8 *) (key + 1), extra_len, &kde) <
	    0) {
		wpa_printf(MSG_INFO, "RSN: Failed to parse KDEs in SMK Error");
		return -1;
	}

	if (kde.error == NULL || kde.error_len < sizeof(error)) {
		wpa_printf(MSG_INFO, "RSN: No Error KDE in SMK Error");
		return -1;
	}

	if (kde.mac_addr && kde.mac_addr_len >= ETH_ALEN)
		os_memcpy(peer, kde.mac_addr, ETH_ALEN);
	else
		os_memset(peer, 0, ETH_ALEN);
	os_memcpy(&error, kde.error, sizeof(error));
	error_type = be_to_host16(error.error_type);
	wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
		"RSN: SMK Error KDE received: MUI %d error_type %d peer "
		MACSTR,
		be_to_host16(error.mui), error_type,
		MAC2STR(peer));

	if (kde.mac_addr &&
	    (error_type == STK_ERR_STA_NR || error_type == STK_ERR_STA_NRSN ||
	     error_type == STK_ERR_CPHR_NS)) {
		struct wpa_peerkey *peerkey;

		for (peerkey = sm->peerkey; peerkey; peerkey = peerkey->next) {
			if (os_memcmp(peerkey->addr, kde.mac_addr, ETH_ALEN) ==
			    0)
				break;
		}
		if (peerkey == NULL) {
			wpa_printf(MSG_DEBUG, "RSN: No matching SMK handshake "
				   "found for SMK Error");
			return -1;
		}
		/* TODO: abort SMK/STK handshake and remove all related keys */
	}

	return 0;
}


static void wpa_supplicant_process_stk_1_of_4(struct wpa_sm *sm,
					      struct wpa_peerkey *peerkey,
					      const struct wpa_eapol_key *key,
					      u16 ver)
{
	struct wpa_eapol_ie_parse ie;
	const u8 *kde;
	size_t len, kde_buf_len;
	struct wpa_ptk *stk;
	u8 buf[8], *kde_buf, *pos;
	be32 lifetime;

	wpa_printf(MSG_DEBUG, "RSN: RX message 1 of STK 4-Way Handshake from "
		   MACSTR " (ver=%d)", MAC2STR(peerkey->addr), ver);

	os_memset(&ie, 0, sizeof(ie));

	/* RSN: msg 1/4 should contain SMKID for the selected SMK */
	kde = (const u8 *) (key + 1);
	len = WPA_GET_BE16(key->key_data_length);
	wpa_hexdump(MSG_DEBUG, "RSN: msg 1/4 key data", kde, len);
	if (wpa_supplicant_parse_ies(kde, len, &ie) < 0 || ie.pmkid == NULL) {
		wpa_printf(MSG_DEBUG, "RSN: No SMKID in STK 1/4");
		return;
	}
	if (os_memcmp(ie.pmkid, peerkey->smkid, PMKID_LEN) != 0) {
		wpa_hexdump(MSG_DEBUG, "RSN: Unknown SMKID in STK 1/4",
			    ie.pmkid, PMKID_LEN);
		return;
	}

	if (random_get_bytes(peerkey->pnonce, WPA_NONCE_LEN)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"RSN: Failed to get random data for PNonce");
		return;
	}
	wpa_hexdump(MSG_DEBUG, "WPA: Renewed PNonce",
		    peerkey->pnonce, WPA_NONCE_LEN);

	/* Calculate STK which will be stored as a temporary STK until it has
	 * been verified when processing message 3/4. */
	stk = &peerkey->tstk;
	wpa_pmk_to_ptk(peerkey->smk, PMK_LEN, "Peer key expansion",
		       sm->own_addr, peerkey->addr,
		       peerkey->pnonce, key->key_nonce,
		       (u8 *) stk, sizeof(*stk),
		       peerkey->use_sha256);
	/* Supplicant: swap tx/rx Mic keys */
	os_memcpy(buf, stk->u.auth.tx_mic_key, 8);
	os_memcpy(stk->u.auth.tx_mic_key, stk->u.auth.rx_mic_key, 8);
	os_memcpy(stk->u.auth.rx_mic_key, buf, 8);
	peerkey->tstk_set = 1;

	kde_buf_len = peerkey->rsnie_p_len +
		2 + RSN_SELECTOR_LEN + sizeof(lifetime) +
		2 + RSN_SELECTOR_LEN + PMKID_LEN;
	kde_buf = os_malloc(kde_buf_len);
	if (kde_buf == NULL)
		return;
	pos = kde_buf;
	pos = wpa_add_ie(pos, peerkey->rsnie_p, peerkey->rsnie_p_len);
	lifetime = host_to_be32(peerkey->lifetime);
	pos = wpa_add_kde(pos, RSN_KEY_DATA_LIFETIME,
			  (u8 *) &lifetime, sizeof(lifetime));
	wpa_add_kde(pos, RSN_KEY_DATA_PMKID, peerkey->smkid, PMKID_LEN);

	if (wpa_supplicant_send_2_of_4(sm, peerkey->addr, key, ver,
				       peerkey->pnonce, kde_buf, kde_buf_len,
				       stk)) {
		os_free(kde_buf);
		return;
	}
	os_free(kde_buf);

	os_memcpy(peerkey->inonce, key->key_nonce, WPA_NONCE_LEN);
}


static void wpa_supplicant_update_smk_lifetime(struct wpa_sm *sm,
					       struct wpa_peerkey *peerkey,
					       struct wpa_eapol_ie_parse *kde)
{
	u32 lifetime;
	struct os_time now;

	if (kde->lifetime == NULL || kde->lifetime_len < sizeof(lifetime))
		return;

	lifetime = WPA_GET_BE32(kde->lifetime);

	if (lifetime >= peerkey->lifetime) {
		wpa_printf(MSG_DEBUG, "RSN: Peer used SMK lifetime %u seconds "
			   "which is larger than or equal to own value %u "
			   "seconds - ignored", lifetime, peerkey->lifetime);
		return;
	}

	wpa_printf(MSG_DEBUG, "RSN: Peer used shorter SMK lifetime %u seconds "
		   "(own was %u seconds) - updated",
		   lifetime, peerkey->lifetime);
	peerkey->lifetime = lifetime;

	os_get_time(&now);
	peerkey->expiration = now.sec + lifetime;
	eloop_cancel_timeout(wpa_supplicant_smk_timeout, sm, peerkey);
	eloop_register_timeout(lifetime, 0, wpa_supplicant_smk_timeout,
			       sm, peerkey);
}


static void wpa_supplicant_process_stk_2_of_4(struct wpa_sm *sm,
					      struct wpa_peerkey *peerkey,
					      const struct wpa_eapol_key *key,
					      u16 ver)
{
	struct wpa_eapol_ie_parse kde;
	const u8 *keydata;
	size_t len;

	wpa_printf(MSG_DEBUG, "RSN: RX message 2 of STK 4-Way Handshake from "
		   MACSTR " (ver=%d)", MAC2STR(peerkey->addr), ver);

	os_memset(&kde, 0, sizeof(kde));

	/* RSN: msg 2/4 should contain SMKID for the selected SMK and RSN IE
	 * from the peer. It may also include Lifetime KDE. */
	keydata = (const u8 *) (key + 1);
	len = WPA_GET_BE16(key->key_data_length);
	wpa_hexdump(MSG_DEBUG, "RSN: msg 2/4 key data", keydata, len);
	if (wpa_supplicant_parse_ies(keydata, len, &kde) < 0 ||
	    kde.pmkid == NULL || kde.rsn_ie == NULL) {
		wpa_printf(MSG_DEBUG, "RSN: No SMKID or RSN IE in STK 2/4");
		return;
	}

	if (os_memcmp(kde.pmkid, peerkey->smkid, PMKID_LEN) != 0) {
		wpa_hexdump(MSG_DEBUG, "RSN: Unknown SMKID in STK 2/4",
			    kde.pmkid, PMKID_LEN);
		return;
	}

	if (kde.rsn_ie_len != peerkey->rsnie_p_len ||
	    os_memcmp(kde.rsn_ie, peerkey->rsnie_p, kde.rsn_ie_len) != 0) {
		wpa_printf(MSG_INFO, "RSN: Peer RSN IE in SMK and STK "
			   "handshakes did not match");
		wpa_hexdump(MSG_DEBUG, "RSN: Peer RSN IE in SMK handshake",
			    peerkey->rsnie_p, peerkey->rsnie_p_len);
		wpa_hexdump(MSG_DEBUG, "RSN: Peer RSN IE in STK handshake",
			    kde.rsn_ie, kde.rsn_ie_len);
		return;
	}

	wpa_supplicant_update_smk_lifetime(sm, peerkey, &kde);

	wpa_supplicant_send_stk_3_of_4(sm, peerkey);
	os_memcpy(peerkey->pnonce, key->key_nonce, WPA_NONCE_LEN);
}


static void wpa_supplicant_process_stk_3_of_4(struct wpa_sm *sm,
					      struct wpa_peerkey *peerkey,
					      const struct wpa_eapol_key *key,
					      u16 ver)
{
	struct wpa_eapol_ie_parse kde;
	const u8 *keydata;
	size_t len, key_len;
	const u8 *_key;
	u8 key_buf[32], rsc[6];

	wpa_printf(MSG_DEBUG, "RSN: RX message 3 of STK 4-Way Handshake from "
		   MACSTR " (ver=%d)", MAC2STR(peerkey->addr), ver);

	os_memset(&kde, 0, sizeof(kde));

	/* RSN: msg 3/4 should contain Initiator RSN IE. It may also include
	 * Lifetime KDE. */
	keydata = (const u8 *) (key + 1);
	len = WPA_GET_BE16(key->key_data_length);
	wpa_hexdump(MSG_DEBUG, "RSN: msg 3/4 key data", keydata, len);
	if (wpa_supplicant_parse_ies(keydata, len, &kde) < 0) {
		wpa_printf(MSG_DEBUG, "RSN: Failed to parse key data in "
			   "STK 3/4");
		return;
	}

	if (kde.rsn_ie_len != peerkey->rsnie_i_len ||
	    os_memcmp(kde.rsn_ie, peerkey->rsnie_i, kde.rsn_ie_len) != 0) {
		wpa_printf(MSG_INFO, "RSN: Initiator RSN IE in SMK and STK "
			   "handshakes did not match");
		wpa_hexdump(MSG_DEBUG, "RSN: Initiator RSN IE in SMK "
			    "handshake",
			    peerkey->rsnie_i, peerkey->rsnie_i_len);
		wpa_hexdump(MSG_DEBUG, "RSN: Initiator RSN IE in STK "
			    "handshake",
			    kde.rsn_ie, kde.rsn_ie_len);
		return;
	}

	if (os_memcmp(peerkey->inonce, key->key_nonce, WPA_NONCE_LEN) != 0) {
		wpa_printf(MSG_WARNING, "RSN: INonce from message 1 of STK "
			   "4-Way Handshake differs from 3 of STK 4-Way "
			   "Handshake - drop packet (src=" MACSTR ")",
			   MAC2STR(peerkey->addr));
		return;
	}

	wpa_supplicant_update_smk_lifetime(sm, peerkey, &kde);

	if (wpa_supplicant_send_4_of_4(sm, peerkey->addr, key, ver,
				       WPA_GET_BE16(key->key_info),
				       NULL, 0, &peerkey->stk))
		return;

	_key = (u8 *) peerkey->stk.tk1;
	if (peerkey->cipher == WPA_CIPHER_TKIP) {
		/* Swap Tx/Rx keys for Michael MIC */
		os_memcpy(key_buf, _key, 16);
		os_memcpy(key_buf + 16, peerkey->stk.u.auth.rx_mic_key, 8);
		os_memcpy(key_buf + 24, peerkey->stk.u.auth.tx_mic_key, 8);
		_key = key_buf;
		key_len = 32;
	} else
		key_len = 16;

	os_memset(rsc, 0, 6);
	if (wpa_sm_set_key(sm, peerkey->cipher, peerkey->addr, 0, 1,
			   rsc, sizeof(rsc), _key, key_len) < 0) {
		wpa_printf(MSG_WARNING, "RSN: Failed to set STK to the "
			   "driver.");
		return;
	}
}


static void wpa_supplicant_process_stk_4_of_4(struct wpa_sm *sm,
					      struct wpa_peerkey *peerkey,
					      const struct wpa_eapol_key *key,
					      u16 ver)
{
	u8 rsc[6];

	wpa_printf(MSG_DEBUG, "RSN: RX message 4 of STK 4-Way Handshake from "
		   MACSTR " (ver=%d)", MAC2STR(peerkey->addr), ver);

	os_memset(rsc, 0, 6);
	if (wpa_sm_set_key(sm, peerkey->cipher, peerkey->addr, 0, 1,
			   rsc, sizeof(rsc), (u8 *) peerkey->stk.tk1,
			   peerkey->cipher == WPA_CIPHER_TKIP ? 32 : 16) < 0) {
		wpa_printf(MSG_WARNING, "RSN: Failed to set STK to the "
			   "driver.");
		return;
	}
}


/**
 * peerkey_verify_eapol_key_mic - Verify PeerKey MIC
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @peerkey: Pointer to the PeerKey data for the peer
 * @key: Pointer to the EAPOL-Key frame header
 * @ver: Version bits from EAPOL-Key Key Info
 * @buf: Pointer to the beginning of EAPOL-Key frame
 * @len: Length of the EAPOL-Key frame
 * Returns: 0 on success, -1 on failure
 */
int peerkey_verify_eapol_key_mic(struct wpa_sm *sm,
				 struct wpa_peerkey *peerkey,
				 struct wpa_eapol_key *key, u16 ver,
				 const u8 *buf, size_t len)
{
	u8 mic[16];
	int ok = 0;

	if (peerkey->initiator && !peerkey->stk_set) {
		wpa_pmk_to_ptk(peerkey->smk, PMK_LEN, "Peer key expansion",
			       sm->own_addr, peerkey->addr,
			       peerkey->inonce, key->key_nonce,
			       (u8 *) &peerkey->stk, sizeof(peerkey->stk),
			       peerkey->use_sha256);
		peerkey->stk_set = 1;
	}

	os_memcpy(mic, key->key_mic, 16);
	if (peerkey->tstk_set) {
		os_memset(key->key_mic, 0, 16);
		wpa_eapol_key_mic(peerkey->tstk.kck, ver, buf, len,
				  key->key_mic);
		if (os_memcmp(mic, key->key_mic, 16) != 0) {
			wpa_printf(MSG_WARNING, "RSN: Invalid EAPOL-Key MIC "
				   "when using TSTK - ignoring TSTK");
		} else {
			ok = 1;
			peerkey->tstk_set = 0;
			peerkey->stk_set = 1;
			os_memcpy(&peerkey->stk, &peerkey->tstk,
				  sizeof(peerkey->stk));
		}
	}

	if (!ok && peerkey->stk_set) {
		os_memset(key->key_mic, 0, 16);
		wpa_eapol_key_mic(peerkey->stk.kck, ver, buf, len,
				  key->key_mic);
		if (os_memcmp(mic, key->key_mic, 16) != 0) {
			wpa_printf(MSG_WARNING, "RSN: Invalid EAPOL-Key MIC "
				   "- dropping packet");
			return -1;
		}
		ok = 1;
	}

	if (!ok) {
		wpa_printf(MSG_WARNING, "RSN: Could not verify EAPOL-Key MIC "
			   "- dropping packet");
		return -1;
	}

	os_memcpy(peerkey->replay_counter, key->replay_counter,
		  WPA_REPLAY_COUNTER_LEN);
	peerkey->replay_counter_set = 1;
	return 0;
}


/**
 * wpa_sm_stkstart - Send EAPOL-Key Request for STK handshake (STK M1)
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @peer: MAC address of the peer STA
 * Returns: 0 on success, or -1 on failure
 *
 * Send an EAPOL-Key Request to the current authenticator to start STK
 * handshake with the peer.
 */
int wpa_sm_stkstart(struct wpa_sm *sm, const u8 *peer)
{
	size_t rlen, kde_len;
	struct wpa_eapol_key *req;
	int key_info, ver;
	u8 bssid[ETH_ALEN], *rbuf, *pos, *count_pos;
	u16 count;
	struct rsn_ie_hdr *hdr;
	struct wpa_peerkey *peerkey;
	struct wpa_ie_data ie;

	if (sm->proto != WPA_PROTO_RSN || !sm->ptk_set || !sm->peerkey_enabled)
		return -1;

	if (sm->ap_rsn_ie &&
	    wpa_parse_wpa_ie_rsn(sm->ap_rsn_ie, sm->ap_rsn_ie_len, &ie) == 0 &&
	    !(ie.capabilities & WPA_CAPABILITY_PEERKEY_ENABLED)) {
		wpa_printf(MSG_DEBUG, "RSN: Current AP does not support STK");
		return -1;
	}

	if (sm->pairwise_cipher == WPA_CIPHER_CCMP)
		ver = WPA_KEY_INFO_TYPE_HMAC_SHA1_AES;
	else
		ver = WPA_KEY_INFO_TYPE_HMAC_MD5_RC4;

	if (wpa_sm_get_bssid(sm, bssid) < 0) {
		wpa_printf(MSG_WARNING, "Failed to read BSSID for EAPOL-Key "
			   "SMK M1");
		return -1;
	}

	/* TODO: find existing entry and if found, use that instead of adding
	 * a new one */
	peerkey = os_zalloc(sizeof(*peerkey));
	if (peerkey == NULL)
		return -1;
	peerkey->initiator = 1;
	os_memcpy(peerkey->addr, peer, ETH_ALEN);
#ifdef CONFIG_IEEE80211W
	if (wpa_key_mgmt_sha256(sm->key_mgmt))
		peerkey->use_sha256 = 1;
#endif /* CONFIG_IEEE80211W */

	/* SMK M1:
	 * EAPOL-Key(S=1, M=1, A=0, I=0, K=0, SM=1, KeyRSC=0, Nonce=INonce,
	 *           MIC=MIC, DataKDs=(RSNIE_I, MAC_P KDE))
	 */

	hdr = (struct rsn_ie_hdr *) peerkey->rsnie_i;
	hdr->elem_id = WLAN_EID_RSN;
	WPA_PUT_LE16(hdr->version, RSN_VERSION);
	pos = (u8 *) (hdr + 1);
	/* Group Suite can be anything for SMK RSN IE; receiver will just
	 * ignore it. */
	RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_CCMP);
	pos += RSN_SELECTOR_LEN;
	count_pos = pos;
	pos += 2;

	count = 0;
	if (sm->allowed_pairwise_cipher & WPA_CIPHER_CCMP) {
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_CCMP);
		pos += RSN_SELECTOR_LEN;
		count++;
	}
	if (sm->allowed_pairwise_cipher & WPA_CIPHER_TKIP) {
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_TKIP);
		pos += RSN_SELECTOR_LEN;
		count++;
	}
	WPA_PUT_LE16(count_pos, count);

	hdr->len = (pos - peerkey->rsnie_i) - 2;
	peerkey->rsnie_i_len = pos - peerkey->rsnie_i;
	wpa_hexdump(MSG_DEBUG, "WPA: RSN IE for SMK handshake",
		    peerkey->rsnie_i, peerkey->rsnie_i_len);

	kde_len = peerkey->rsnie_i_len + 2 + RSN_SELECTOR_LEN + ETH_ALEN;

	rbuf = wpa_sm_alloc_eapol(sm, IEEE802_1X_TYPE_EAPOL_KEY, NULL,
				  sizeof(*req) + kde_len, &rlen,
				  (void *) &req);
	if (rbuf == NULL) {
		wpa_supplicant_peerkey_free(sm, peerkey);
		return -1;
	}

	req->type = EAPOL_KEY_TYPE_RSN;
	key_info = WPA_KEY_INFO_SMK_MESSAGE | WPA_KEY_INFO_MIC |
		WPA_KEY_INFO_SECURE | WPA_KEY_INFO_REQUEST | ver;
	WPA_PUT_BE16(req->key_info, key_info);
	WPA_PUT_BE16(req->key_length, 0);
	os_memcpy(req->replay_counter, sm->request_counter,
		  WPA_REPLAY_COUNTER_LEN);
	inc_byte_array(sm->request_counter, WPA_REPLAY_COUNTER_LEN);

	if (random_get_bytes(peerkey->inonce, WPA_NONCE_LEN)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: Failed to get random data for INonce");
		os_free(rbuf);
		wpa_supplicant_peerkey_free(sm, peerkey);
		return -1;
	}
	os_memcpy(req->key_nonce, peerkey->inonce, WPA_NONCE_LEN);
	wpa_hexdump(MSG_DEBUG, "WPA: INonce for SMK handshake",
		    req->key_nonce, WPA_NONCE_LEN);

	WPA_PUT_BE16(req->key_data_length, (u16) kde_len);
	pos = (u8 *) (req + 1);

	/* Initiator RSN IE */
	pos = wpa_add_ie(pos, peerkey->rsnie_i, peerkey->rsnie_i_len);
	/* Peer MAC address KDE */
	wpa_add_kde(pos, RSN_KEY_DATA_MAC_ADDR, peer, ETH_ALEN);

	wpa_printf(MSG_INFO, "RSN: Sending EAPOL-Key SMK M1 Request (peer "
		   MACSTR ")", MAC2STR(peer));
	wpa_eapol_key_send(sm, sm->ptk.kck, ver, bssid, ETH_P_EAPOL,
			   rbuf, rlen, req->key_mic);

	peerkey->next = sm->peerkey;
	sm->peerkey = peerkey;

	return 0;
}


/**
 * peerkey_deinit - Free PeerKey values
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 */
void peerkey_deinit(struct wpa_sm *sm)
{
	struct wpa_peerkey *prev, *peerkey = sm->peerkey;
	while (peerkey) {
		prev = peerkey;
		peerkey = peerkey->next;
		os_free(prev);
	}
	sm->peerkey = NULL;
}


void peerkey_rx_eapol_4way(struct wpa_sm *sm, struct wpa_peerkey *peerkey,
			   struct wpa_eapol_key *key, u16 key_info, u16 ver)
{
	if ((key_info & (WPA_KEY_INFO_MIC | WPA_KEY_INFO_ACK)) ==
	    (WPA_KEY_INFO_MIC | WPA_KEY_INFO_ACK)) {
		/* 3/4 STK 4-Way Handshake */
		wpa_supplicant_process_stk_3_of_4(sm, peerkey, key, ver);
	} else if (key_info & WPA_KEY_INFO_ACK) {
		/* 1/4 STK 4-Way Handshake */
		wpa_supplicant_process_stk_1_of_4(sm, peerkey, key, ver);
	} else if (key_info & WPA_KEY_INFO_SECURE) {
		/* 4/4 STK 4-Way Handshake */
		wpa_supplicant_process_stk_4_of_4(sm, peerkey, key, ver);
	} else {
		/* 2/4 STK 4-Way Handshake */
		wpa_supplicant_process_stk_2_of_4(sm, peerkey, key, ver);
	}
}


void peerkey_rx_eapol_smk(struct wpa_sm *sm, const u8 *src_addr,
			  struct wpa_eapol_key *key, size_t extra_len,
			  u16 key_info, u16 ver)
{
	if (key_info & WPA_KEY_INFO_ERROR) {
		/* SMK Error */
		wpa_supplicant_process_smk_error(sm, src_addr, key, extra_len);
	} else if (key_info & WPA_KEY_INFO_ACK) {
		/* SMK M2 */
		wpa_supplicant_process_smk_m2(sm, src_addr, key, extra_len,
					      ver);
	} else {
		/* SMK M4 or M5 */
		wpa_supplicant_process_smk_m45(sm, src_addr, key, extra_len,
					       ver);
	}
}

#endif /* CONFIG_PEERKEY */
