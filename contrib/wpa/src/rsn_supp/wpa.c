/*
 * WPA Supplicant - WPA state machine and EAPOL-Key processing
 * Copyright (c) 2003-2018, Jouni Malinen <j@w1.fi>
 * Copyright(c) 2015 Intel Deutschland GmbH
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto/aes.h"
#include "crypto/aes_wrap.h"
#include "crypto/crypto.h"
#include "crypto/random.h"
#include "crypto/aes_siv.h"
#include "crypto/sha256.h"
#include "crypto/sha384.h"
#include "crypto/sha512.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "eap_common/eap_defs.h"
#include "eapol_supp/eapol_supp_sm.h"
#include "wpa.h"
#include "eloop.h"
#include "preauth.h"
#include "pmksa_cache.h"
#include "wpa_i.h"
#include "wpa_ie.h"


static const u8 null_rsc[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };


/**
 * wpa_eapol_key_send - Send WPA/RSN EAPOL-Key message
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @ptk: PTK for Key Confirmation/Encryption Key
 * @ver: Version field from Key Info
 * @dest: Destination address for the frame
 * @proto: Ethertype (usually ETH_P_EAPOL)
 * @msg: EAPOL-Key message
 * @msg_len: Length of message
 * @key_mic: Pointer to the buffer to which the EAPOL-Key MIC is written
 * Returns: >= 0 on success, < 0 on failure
 */
int wpa_eapol_key_send(struct wpa_sm *sm, struct wpa_ptk *ptk,
		       int ver, const u8 *dest, u16 proto,
		       u8 *msg, size_t msg_len, u8 *key_mic)
{
	int ret = -1;
	size_t mic_len = wpa_mic_len(sm->key_mgmt, sm->pmk_len);

	wpa_printf(MSG_DEBUG, "WPA: Send EAPOL-Key frame to " MACSTR
		   " ver=%d mic_len=%d key_mgmt=0x%x",
		   MAC2STR(dest), ver, (int) mic_len, sm->key_mgmt);
	if (is_zero_ether_addr(dest) && is_zero_ether_addr(sm->bssid)) {
		/*
		 * Association event was not yet received; try to fetch
		 * BSSID from the driver.
		 */
		if (wpa_sm_get_bssid(sm, sm->bssid) < 0) {
			wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
				"WPA: Failed to read BSSID for "
				"EAPOL-Key destination address");
		} else {
			dest = sm->bssid;
			wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
				"WPA: Use BSSID (" MACSTR
				") as the destination for EAPOL-Key",
				MAC2STR(dest));
		}
	}

	if (mic_len) {
		if (key_mic && (!ptk || !ptk->kck_len))
			goto out;

		if (key_mic &&
		    wpa_eapol_key_mic(ptk->kck, ptk->kck_len, sm->key_mgmt, ver,
				      msg, msg_len, key_mic)) {
			wpa_msg(sm->ctx->msg_ctx, MSG_ERROR,
				"WPA: Failed to generate EAPOL-Key version %d key_mgmt 0x%x MIC",
				ver, sm->key_mgmt);
			goto out;
		}
		if (ptk)
			wpa_hexdump_key(MSG_DEBUG, "WPA: KCK",
					ptk->kck, ptk->kck_len);
		wpa_hexdump(MSG_DEBUG, "WPA: Derived Key MIC",
			    key_mic, mic_len);
	} else {
#ifdef CONFIG_FILS
		/* AEAD cipher - Key MIC field not used */
		struct ieee802_1x_hdr *s_hdr, *hdr;
		struct wpa_eapol_key *s_key, *key;
		u8 *buf, *s_key_data, *key_data;
		size_t buf_len = msg_len + AES_BLOCK_SIZE;
		size_t key_data_len;
		u16 eapol_len;
		const u8 *aad[1];
		size_t aad_len[1];

		if (!ptk || !ptk->kek_len)
			goto out;

		key_data_len = msg_len - sizeof(struct ieee802_1x_hdr) -
			sizeof(struct wpa_eapol_key) - 2;

		buf = os_malloc(buf_len);
		if (!buf)
			goto out;

		os_memcpy(buf, msg, msg_len);
		hdr = (struct ieee802_1x_hdr *) buf;
		key = (struct wpa_eapol_key *) (hdr + 1);
		key_data = ((u8 *) (key + 1)) + 2;

		/* Update EAPOL header to include AES-SIV overhead */
		eapol_len = be_to_host16(hdr->length);
		eapol_len += AES_BLOCK_SIZE;
		hdr->length = host_to_be16(eapol_len);

		/* Update Key Data Length field to include AES-SIV overhead */
		WPA_PUT_BE16((u8 *) (key + 1), AES_BLOCK_SIZE + key_data_len);

		s_hdr = (struct ieee802_1x_hdr *) msg;
		s_key = (struct wpa_eapol_key *) (s_hdr + 1);
		s_key_data = ((u8 *) (s_key + 1)) + 2;

		wpa_hexdump_key(MSG_DEBUG, "WPA: Plaintext Key Data",
				s_key_data, key_data_len);

		wpa_hexdump_key(MSG_DEBUG, "WPA: KEK", ptk->kek, ptk->kek_len);
		 /* AES-SIV AAD from EAPOL protocol version field (inclusive) to
		  * to Key Data (exclusive). */
		aad[0] = buf;
		aad_len[0] = key_data - buf;
		if (aes_siv_encrypt(ptk->kek, ptk->kek_len,
				    s_key_data, key_data_len,
				    1, aad, aad_len, key_data) < 0) {
			os_free(buf);
			goto out;
		}

		wpa_hexdump(MSG_DEBUG, "WPA: Encrypted Key Data from SIV",
			    key_data, AES_BLOCK_SIZE + key_data_len);

		os_free(msg);
		msg = buf;
		msg_len = buf_len;
#else /* CONFIG_FILS */
		goto out;
#endif /* CONFIG_FILS */
	}

	wpa_hexdump(MSG_MSGDUMP, "WPA: TX EAPOL-Key", msg, msg_len);
	ret = wpa_sm_ether_send(sm, dest, proto, msg, msg_len);
	eapol_sm_notify_tx_eapol_key(sm->eapol);
out:
	os_free(msg);
	return ret;
}


/**
 * wpa_sm_key_request - Send EAPOL-Key Request
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @error: Indicate whether this is an Michael MIC error report
 * @pairwise: 1 = error report for pairwise packet, 0 = for group packet
 *
 * Send an EAPOL-Key Request to the current authenticator. This function is
 * used to request rekeying and it is usually called when a local Michael MIC
 * failure is detected.
 */
void wpa_sm_key_request(struct wpa_sm *sm, int error, int pairwise)
{
	size_t mic_len, hdrlen, rlen;
	struct wpa_eapol_key *reply;
	int key_info, ver;
	u8 bssid[ETH_ALEN], *rbuf, *key_mic, *mic;

	if (wpa_use_akm_defined(sm->key_mgmt))
		ver = WPA_KEY_INFO_TYPE_AKM_DEFINED;
	else if (wpa_key_mgmt_ft(sm->key_mgmt) ||
		 wpa_key_mgmt_sha256(sm->key_mgmt))
		ver = WPA_KEY_INFO_TYPE_AES_128_CMAC;
	else if (sm->pairwise_cipher != WPA_CIPHER_TKIP)
		ver = WPA_KEY_INFO_TYPE_HMAC_SHA1_AES;
	else
		ver = WPA_KEY_INFO_TYPE_HMAC_MD5_RC4;

	if (wpa_sm_get_bssid(sm, bssid) < 0) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"Failed to read BSSID for EAPOL-Key request");
		return;
	}

	mic_len = wpa_mic_len(sm->key_mgmt, sm->pmk_len);
	hdrlen = sizeof(*reply) + mic_len + 2;
	rbuf = wpa_sm_alloc_eapol(sm, IEEE802_1X_TYPE_EAPOL_KEY, NULL,
				  hdrlen, &rlen, (void *) &reply);
	if (rbuf == NULL)
		return;

	reply->type = (sm->proto == WPA_PROTO_RSN ||
		       sm->proto == WPA_PROTO_OSEN) ?
		EAPOL_KEY_TYPE_RSN : EAPOL_KEY_TYPE_WPA;
	key_info = WPA_KEY_INFO_REQUEST | ver;
	if (sm->ptk_set)
		key_info |= WPA_KEY_INFO_SECURE;
	if (sm->ptk_set && mic_len)
		key_info |= WPA_KEY_INFO_MIC;
	if (error)
		key_info |= WPA_KEY_INFO_ERROR;
	if (pairwise)
		key_info |= WPA_KEY_INFO_KEY_TYPE;
	WPA_PUT_BE16(reply->key_info, key_info);
	WPA_PUT_BE16(reply->key_length, 0);
	os_memcpy(reply->replay_counter, sm->request_counter,
		  WPA_REPLAY_COUNTER_LEN);
	inc_byte_array(sm->request_counter, WPA_REPLAY_COUNTER_LEN);

	mic = (u8 *) (reply + 1);
	WPA_PUT_BE16(mic + mic_len, 0);
	if (!(key_info & WPA_KEY_INFO_MIC))
		key_mic = NULL;
	else
		key_mic = mic;

	wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
		"WPA: Sending EAPOL-Key Request (error=%d "
		"pairwise=%d ptk_set=%d len=%lu)",
		error, pairwise, sm->ptk_set, (unsigned long) rlen);
	wpa_eapol_key_send(sm, &sm->ptk, ver, bssid, ETH_P_EAPOL, rbuf, rlen,
			   key_mic);
}


static void wpa_supplicant_key_mgmt_set_pmk(struct wpa_sm *sm)
{
#ifdef CONFIG_IEEE80211R
	if (sm->key_mgmt == WPA_KEY_MGMT_FT_IEEE8021X) {
		if (wpa_sm_key_mgmt_set_pmk(sm, sm->xxkey, sm->xxkey_len))
			wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
				"RSN: Cannot set low order 256 bits of MSK for key management offload");
	} else {
#endif /* CONFIG_IEEE80211R */
		if (wpa_sm_key_mgmt_set_pmk(sm, sm->pmk, sm->pmk_len))
			wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
				"RSN: Cannot set PMK for key management offload");
#ifdef CONFIG_IEEE80211R
	}
#endif /* CONFIG_IEEE80211R */
}


static int wpa_supplicant_get_pmk(struct wpa_sm *sm,
				  const unsigned char *src_addr,
				  const u8 *pmkid)
{
	int abort_cached = 0;

	if (pmkid && !sm->cur_pmksa) {
		/* When using drivers that generate RSN IE, wpa_supplicant may
		 * not have enough time to get the association information
		 * event before receiving this 1/4 message, so try to find a
		 * matching PMKSA cache entry here. */
		sm->cur_pmksa = pmksa_cache_get(sm->pmksa, src_addr, pmkid,
						NULL, 0);
		if (sm->cur_pmksa) {
			wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
				"RSN: found matching PMKID from PMKSA cache");
		} else {
			wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
				"RSN: no matching PMKID found");
			abort_cached = 1;
		}
	}

	if (pmkid && sm->cur_pmksa &&
	    os_memcmp_const(pmkid, sm->cur_pmksa->pmkid, PMKID_LEN) == 0) {
		wpa_hexdump(MSG_DEBUG, "RSN: matched PMKID", pmkid, PMKID_LEN);
		wpa_sm_set_pmk_from_pmksa(sm);
		wpa_hexdump_key(MSG_DEBUG, "RSN: PMK from PMKSA cache",
				sm->pmk, sm->pmk_len);
		eapol_sm_notify_cached(sm->eapol);
#ifdef CONFIG_IEEE80211R
		sm->xxkey_len = 0;
#ifdef CONFIG_SAE
		if (sm->key_mgmt == WPA_KEY_MGMT_FT_SAE &&
		    sm->pmk_len == PMK_LEN) {
			/* Need to allow FT key derivation to proceed with
			 * PMK from SAE being used as the XXKey in cases where
			 * the PMKID in msg 1/4 matches the PMKSA entry that was
			 * just added based on SAE authentication for the
			 * initial mobility domain association. */
			os_memcpy(sm->xxkey, sm->pmk, sm->pmk_len);
			sm->xxkey_len = sm->pmk_len;
		}
#endif /* CONFIG_SAE */
#endif /* CONFIG_IEEE80211R */
	} else if (wpa_key_mgmt_wpa_ieee8021x(sm->key_mgmt) && sm->eapol) {
		int res, pmk_len;

		if (wpa_key_mgmt_sha384(sm->key_mgmt))
			pmk_len = PMK_LEN_SUITE_B_192;
		else
			pmk_len = PMK_LEN;
		res = eapol_sm_get_key(sm->eapol, sm->pmk, pmk_len);
		if (res) {
			if (pmk_len == PMK_LEN) {
				/*
				 * EAP-LEAP is an exception from other EAP
				 * methods: it uses only 16-byte PMK.
				 */
				res = eapol_sm_get_key(sm->eapol, sm->pmk, 16);
				pmk_len = 16;
			}
		} else {
#ifdef CONFIG_IEEE80211R
			u8 buf[2 * PMK_LEN];
			if (eapol_sm_get_key(sm->eapol, buf, 2 * PMK_LEN) == 0)
			{
				if (wpa_key_mgmt_sha384(sm->key_mgmt)) {
					os_memcpy(sm->xxkey, buf,
						  SHA384_MAC_LEN);
					sm->xxkey_len = SHA384_MAC_LEN;
				} else {
					os_memcpy(sm->xxkey, buf + PMK_LEN,
						  PMK_LEN);
					sm->xxkey_len = PMK_LEN;
				}
				os_memset(buf, 0, sizeof(buf));
			}
#endif /* CONFIG_IEEE80211R */
		}
		if (res == 0) {
			struct rsn_pmksa_cache_entry *sa = NULL;
			const u8 *fils_cache_id = NULL;

#ifdef CONFIG_FILS
			if (sm->fils_cache_id_set)
				fils_cache_id = sm->fils_cache_id;
#endif /* CONFIG_FILS */

			wpa_hexdump_key(MSG_DEBUG, "WPA: PMK from EAPOL state "
					"machines", sm->pmk, pmk_len);
			sm->pmk_len = pmk_len;
			wpa_supplicant_key_mgmt_set_pmk(sm);
			if (sm->proto == WPA_PROTO_RSN &&
			    !wpa_key_mgmt_suite_b(sm->key_mgmt) &&
			    !wpa_key_mgmt_ft(sm->key_mgmt)) {
				sa = pmksa_cache_add(sm->pmksa,
						     sm->pmk, pmk_len, NULL,
						     NULL, 0,
						     src_addr, sm->own_addr,
						     sm->network_ctx,
						     sm->key_mgmt,
						     fils_cache_id);
			}
			if (!sm->cur_pmksa && pmkid &&
			    pmksa_cache_get(sm->pmksa, src_addr, pmkid, NULL,
				    0)) {
				wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
					"RSN: the new PMK matches with the "
					"PMKID");
				abort_cached = 0;
			} else if (sa && !sm->cur_pmksa && pmkid) {
				/*
				 * It looks like the authentication server
				 * derived mismatching MSK. This should not
				 * really happen, but bugs happen.. There is not
				 * much we can do here without knowing what
				 * exactly caused the server to misbehave.
				 */
				wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
					"RSN: PMKID mismatch - authentication server may have derived different MSK?!");
				return -1;
			}

			if (!sm->cur_pmksa)
				sm->cur_pmksa = sa;
		} else {
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: Failed to get master session key from "
				"EAPOL state machines - key handshake "
				"aborted");
			if (sm->cur_pmksa) {
				wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
					"RSN: Cancelled PMKSA caching "
					"attempt");
				sm->cur_pmksa = NULL;
				abort_cached = 1;
			} else if (!abort_cached) {
				return -1;
			}
		}
	}

	if (abort_cached && wpa_key_mgmt_wpa_ieee8021x(sm->key_mgmt) &&
	    !wpa_key_mgmt_suite_b(sm->key_mgmt) &&
	    !wpa_key_mgmt_ft(sm->key_mgmt) && sm->key_mgmt != WPA_KEY_MGMT_OSEN)
	{
		/* Send EAPOL-Start to trigger full EAP authentication. */
		u8 *buf;
		size_t buflen;

		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"RSN: no PMKSA entry found - trigger "
			"full EAP authentication");
		buf = wpa_sm_alloc_eapol(sm, IEEE802_1X_TYPE_EAPOL_START,
					 NULL, 0, &buflen, NULL);
		if (buf) {
			wpa_sm_ether_send(sm, sm->bssid, ETH_P_EAPOL,
					  buf, buflen);
			os_free(buf);
			return -2;
		}

		return -1;
	}

	return 0;
}


/**
 * wpa_supplicant_send_2_of_4 - Send message 2 of WPA/RSN 4-Way Handshake
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @dst: Destination address for the frame
 * @key: Pointer to the EAPOL-Key frame header
 * @ver: Version bits from EAPOL-Key Key Info
 * @nonce: Nonce value for the EAPOL-Key frame
 * @wpa_ie: WPA/RSN IE
 * @wpa_ie_len: Length of the WPA/RSN IE
 * @ptk: PTK to use for keyed hash and encryption
 * Returns: >= 0 on success, < 0 on failure
 */
int wpa_supplicant_send_2_of_4(struct wpa_sm *sm, const unsigned char *dst,
			       const struct wpa_eapol_key *key,
			       int ver, const u8 *nonce,
			       const u8 *wpa_ie, size_t wpa_ie_len,
			       struct wpa_ptk *ptk)
{
	size_t mic_len, hdrlen, rlen;
	struct wpa_eapol_key *reply;
	u8 *rbuf, *key_mic;
	u8 *rsn_ie_buf = NULL;
	u16 key_info;

	if (wpa_ie == NULL) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING, "WPA: No wpa_ie set - "
			"cannot generate msg 2/4");
		return -1;
	}

#ifdef CONFIG_IEEE80211R
	if (wpa_key_mgmt_ft(sm->key_mgmt)) {
		int res;

		/*
		 * Add PMKR1Name into RSN IE (PMKID-List) and add MDIE and
		 * FTIE from (Re)Association Response.
		 */
		rsn_ie_buf = os_malloc(wpa_ie_len + 2 + 2 + PMKID_LEN +
				       sm->assoc_resp_ies_len);
		if (rsn_ie_buf == NULL)
			return -1;
		os_memcpy(rsn_ie_buf, wpa_ie, wpa_ie_len);
		res = wpa_insert_pmkid(rsn_ie_buf, &wpa_ie_len,
				       sm->pmk_r1_name);
		if (res < 0) {
			os_free(rsn_ie_buf);
			return -1;
		}

		if (sm->assoc_resp_ies) {
			os_memcpy(rsn_ie_buf + wpa_ie_len, sm->assoc_resp_ies,
				  sm->assoc_resp_ies_len);
			wpa_ie_len += sm->assoc_resp_ies_len;
		}

		wpa_ie = rsn_ie_buf;
	}
#endif /* CONFIG_IEEE80211R */

	wpa_hexdump(MSG_DEBUG, "WPA: WPA IE for msg 2/4", wpa_ie, wpa_ie_len);

	mic_len = wpa_mic_len(sm->key_mgmt, sm->pmk_len);
	hdrlen = sizeof(*reply) + mic_len + 2;
	rbuf = wpa_sm_alloc_eapol(sm, IEEE802_1X_TYPE_EAPOL_KEY,
				  NULL, hdrlen + wpa_ie_len,
				  &rlen, (void *) &reply);
	if (rbuf == NULL) {
		os_free(rsn_ie_buf);
		return -1;
	}

	reply->type = (sm->proto == WPA_PROTO_RSN ||
		       sm->proto == WPA_PROTO_OSEN) ?
		EAPOL_KEY_TYPE_RSN : EAPOL_KEY_TYPE_WPA;
	key_info = ver | WPA_KEY_INFO_KEY_TYPE;
	if (mic_len)
		key_info |= WPA_KEY_INFO_MIC;
	else
		key_info |= WPA_KEY_INFO_ENCR_KEY_DATA;
	WPA_PUT_BE16(reply->key_info, key_info);
	if (sm->proto == WPA_PROTO_RSN || sm->proto == WPA_PROTO_OSEN)
		WPA_PUT_BE16(reply->key_length, 0);
	else
		os_memcpy(reply->key_length, key->key_length, 2);
	os_memcpy(reply->replay_counter, key->replay_counter,
		  WPA_REPLAY_COUNTER_LEN);
	wpa_hexdump(MSG_DEBUG, "WPA: Replay Counter", reply->replay_counter,
		    WPA_REPLAY_COUNTER_LEN);

	key_mic = (u8 *) (reply + 1);
	WPA_PUT_BE16(key_mic + mic_len, wpa_ie_len); /* Key Data Length */
	os_memcpy(key_mic + mic_len + 2, wpa_ie, wpa_ie_len); /* Key Data */
	os_free(rsn_ie_buf);

	os_memcpy(reply->key_nonce, nonce, WPA_NONCE_LEN);

	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "WPA: Sending EAPOL-Key 2/4");
	return wpa_eapol_key_send(sm, ptk, ver, dst, ETH_P_EAPOL, rbuf, rlen,
				  key_mic);
}


static int wpa_derive_ptk(struct wpa_sm *sm, const unsigned char *src_addr,
			  const struct wpa_eapol_key *key, struct wpa_ptk *ptk)
{
#ifdef CONFIG_IEEE80211R
	if (wpa_key_mgmt_ft(sm->key_mgmt))
		return wpa_derive_ptk_ft(sm, src_addr, key, ptk);
#endif /* CONFIG_IEEE80211R */

	return wpa_pmk_to_ptk(sm->pmk, sm->pmk_len, "Pairwise key expansion",
			      sm->own_addr, sm->bssid, sm->snonce,
			      key->key_nonce, ptk, sm->key_mgmt,
			      sm->pairwise_cipher);
}


static void wpa_supplicant_process_1_of_4(struct wpa_sm *sm,
					  const unsigned char *src_addr,
					  const struct wpa_eapol_key *key,
					  u16 ver, const u8 *key_data,
					  size_t key_data_len)
{
	struct wpa_eapol_ie_parse ie;
	struct wpa_ptk *ptk;
	int res;
	u8 *kde, *kde_buf = NULL;
	size_t kde_len;

	if (wpa_sm_get_network_ctx(sm) == NULL) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING, "WPA: No SSID info "
			"found (msg 1 of 4)");
		return;
	}

	wpa_sm_set_state(sm, WPA_4WAY_HANDSHAKE);
	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "WPA: RX message 1 of 4-Way "
		"Handshake from " MACSTR " (ver=%d)", MAC2STR(src_addr), ver);

	os_memset(&ie, 0, sizeof(ie));

	if (sm->proto == WPA_PROTO_RSN || sm->proto == WPA_PROTO_OSEN) {
		/* RSN: msg 1/4 should contain PMKID for the selected PMK */
		wpa_hexdump(MSG_DEBUG, "RSN: msg 1/4 key data",
			    key_data, key_data_len);
		if (wpa_supplicant_parse_ies(key_data, key_data_len, &ie) < 0)
			goto failed;
		if (ie.pmkid) {
			wpa_hexdump(MSG_DEBUG, "RSN: PMKID from "
				    "Authenticator", ie.pmkid, PMKID_LEN);
		}
	}

	res = wpa_supplicant_get_pmk(sm, src_addr, ie.pmkid);
	if (res == -2) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "RSN: Do not reply to "
			"msg 1/4 - requesting full EAP authentication");
		return;
	}
	if (res)
		goto failed;

	if (sm->renew_snonce) {
		if (random_get_bytes(sm->snonce, WPA_NONCE_LEN)) {
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: Failed to get random data for SNonce");
			goto failed;
		}
		sm->renew_snonce = 0;
		wpa_hexdump(MSG_DEBUG, "WPA: Renewed SNonce",
			    sm->snonce, WPA_NONCE_LEN);
	}

	/* Calculate PTK which will be stored as a temporary PTK until it has
	 * been verified when processing message 3/4. */
	ptk = &sm->tptk;
	if (wpa_derive_ptk(sm, src_addr, key, ptk) < 0)
		goto failed;
	if (sm->pairwise_cipher == WPA_CIPHER_TKIP) {
		u8 buf[8];
		/* Supplicant: swap tx/rx Mic keys */
		os_memcpy(buf, &ptk->tk[16], 8);
		os_memcpy(&ptk->tk[16], &ptk->tk[24], 8);
		os_memcpy(&ptk->tk[24], buf, 8);
		os_memset(buf, 0, sizeof(buf));
	}
	sm->tptk_set = 1;

	kde = sm->assoc_wpa_ie;
	kde_len = sm->assoc_wpa_ie_len;

#ifdef CONFIG_P2P
	if (sm->p2p) {
		kde_buf = os_malloc(kde_len + 2 + RSN_SELECTOR_LEN + 1);
		if (kde_buf) {
			u8 *pos;
			wpa_printf(MSG_DEBUG, "P2P: Add IP Address Request KDE "
				   "into EAPOL-Key 2/4");
			os_memcpy(kde_buf, kde, kde_len);
			kde = kde_buf;
			pos = kde + kde_len;
			*pos++ = WLAN_EID_VENDOR_SPECIFIC;
			*pos++ = RSN_SELECTOR_LEN + 1;
			RSN_SELECTOR_PUT(pos, WFA_KEY_DATA_IP_ADDR_REQ);
			pos += RSN_SELECTOR_LEN;
			*pos++ = 0x01;
			kde_len = pos - kde;
		}
	}
#endif /* CONFIG_P2P */

	if (wpa_supplicant_send_2_of_4(sm, sm->bssid, key, ver, sm->snonce,
				       kde, kde_len, ptk) < 0)
		goto failed;

	os_free(kde_buf);
	os_memcpy(sm->anonce, key->key_nonce, WPA_NONCE_LEN);
	return;

failed:
	os_free(kde_buf);
	wpa_sm_deauthenticate(sm, WLAN_REASON_UNSPECIFIED);
}


static void wpa_sm_start_preauth(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_sm *sm = eloop_ctx;
	rsn_preauth_candidate_process(sm);
}


static void wpa_supplicant_key_neg_complete(struct wpa_sm *sm,
					    const u8 *addr, int secure)
{
	wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
		"WPA: Key negotiation completed with "
		MACSTR " [PTK=%s GTK=%s]", MAC2STR(addr),
		wpa_cipher_txt(sm->pairwise_cipher),
		wpa_cipher_txt(sm->group_cipher));
	wpa_sm_cancel_auth_timeout(sm);
	wpa_sm_set_state(sm, WPA_COMPLETED);

	if (secure) {
		wpa_sm_mlme_setprotection(
			sm, addr, MLME_SETPROTECTION_PROTECT_TYPE_RX_TX,
			MLME_SETPROTECTION_KEY_TYPE_PAIRWISE);
		eapol_sm_notify_portValid(sm->eapol, TRUE);
		if (wpa_key_mgmt_wpa_psk(sm->key_mgmt) ||
		    sm->key_mgmt == WPA_KEY_MGMT_DPP ||
		    sm->key_mgmt == WPA_KEY_MGMT_OWE)
			eapol_sm_notify_eap_success(sm->eapol, TRUE);
		/*
		 * Start preauthentication after a short wait to avoid a
		 * possible race condition between the data receive and key
		 * configuration after the 4-Way Handshake. This increases the
		 * likelihood of the first preauth EAPOL-Start frame getting to
		 * the target AP.
		 */
		eloop_register_timeout(1, 0, wpa_sm_start_preauth, sm, NULL);
	}

	if (sm->cur_pmksa && sm->cur_pmksa->opportunistic) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"RSN: Authenticator accepted "
			"opportunistic PMKSA entry - marking it valid");
		sm->cur_pmksa->opportunistic = 0;
	}

#ifdef CONFIG_IEEE80211R
	if (wpa_key_mgmt_ft(sm->key_mgmt)) {
		/* Prepare for the next transition */
		wpa_ft_prepare_auth_request(sm, NULL);
	}
#endif /* CONFIG_IEEE80211R */
}


static void wpa_sm_rekey_ptk(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_sm *sm = eloop_ctx;
	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "WPA: Request PTK rekeying");
	wpa_sm_key_request(sm, 0, 1);
}


static int wpa_supplicant_install_ptk(struct wpa_sm *sm,
				      const struct wpa_eapol_key *key)
{
	int keylen, rsclen;
	enum wpa_alg alg;
	const u8 *key_rsc;

	if (sm->ptk.installed) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"WPA: Do not re-install same PTK to the driver");
		return 0;
	}

	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
		"WPA: Installing PTK to the driver");

	if (sm->pairwise_cipher == WPA_CIPHER_NONE) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "WPA: Pairwise Cipher "
			"Suite: NONE - do not use pairwise keys");
		return 0;
	}

	if (!wpa_cipher_valid_pairwise(sm->pairwise_cipher)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: Unsupported pairwise cipher %d",
			sm->pairwise_cipher);
		return -1;
	}

	alg = wpa_cipher_to_alg(sm->pairwise_cipher);
	keylen = wpa_cipher_key_len(sm->pairwise_cipher);
	if (keylen <= 0 || (unsigned int) keylen != sm->ptk.tk_len) {
		wpa_printf(MSG_DEBUG, "WPA: TK length mismatch: %d != %lu",
			   keylen, (long unsigned int) sm->ptk.tk_len);
		return -1;
	}
	rsclen = wpa_cipher_rsc_len(sm->pairwise_cipher);

	if (sm->proto == WPA_PROTO_RSN || sm->proto == WPA_PROTO_OSEN) {
		key_rsc = null_rsc;
	} else {
		key_rsc = key->key_rsc;
		wpa_hexdump(MSG_DEBUG, "WPA: RSC", key_rsc, rsclen);
	}

	if (wpa_sm_set_key(sm, alg, sm->bssid, 0, 1, key_rsc, rsclen,
			   sm->ptk.tk, keylen) < 0) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: Failed to set PTK to the "
			"driver (alg=%d keylen=%d bssid=" MACSTR ")",
			alg, keylen, MAC2STR(sm->bssid));
		return -1;
	}

	/* TK is not needed anymore in supplicant */
	os_memset(sm->ptk.tk, 0, WPA_TK_MAX_LEN);
	sm->ptk.tk_len = 0;
	sm->ptk.installed = 1;

	if (sm->wpa_ptk_rekey) {
		eloop_cancel_timeout(wpa_sm_rekey_ptk, sm, NULL);
		eloop_register_timeout(sm->wpa_ptk_rekey, 0, wpa_sm_rekey_ptk,
				       sm, NULL);
	}

	return 0;
}


static int wpa_supplicant_check_group_cipher(struct wpa_sm *sm,
					     int group_cipher,
					     int keylen, int maxkeylen,
					     int *key_rsc_len,
					     enum wpa_alg *alg)
{
	int klen;

	*alg = wpa_cipher_to_alg(group_cipher);
	if (*alg == WPA_ALG_NONE) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: Unsupported Group Cipher %d",
			group_cipher);
		return -1;
	}
	*key_rsc_len = wpa_cipher_rsc_len(group_cipher);

	klen = wpa_cipher_key_len(group_cipher);
	if (keylen != klen || maxkeylen < klen) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: Unsupported %s Group Cipher key length %d (%d)",
			wpa_cipher_txt(group_cipher), keylen, maxkeylen);
		return -1;
	}
	return 0;
}


struct wpa_gtk_data {
	enum wpa_alg alg;
	int tx, key_rsc_len, keyidx;
	u8 gtk[32];
	int gtk_len;
};


static int wpa_supplicant_install_gtk(struct wpa_sm *sm,
				      const struct wpa_gtk_data *gd,
				      const u8 *key_rsc, int wnm_sleep)
{
	const u8 *_gtk = gd->gtk;
	u8 gtk_buf[32];

	/* Detect possible key reinstallation */
	if ((sm->gtk.gtk_len == (size_t) gd->gtk_len &&
	     os_memcmp(sm->gtk.gtk, gd->gtk, sm->gtk.gtk_len) == 0) ||
	    (sm->gtk_wnm_sleep.gtk_len == (size_t) gd->gtk_len &&
	     os_memcmp(sm->gtk_wnm_sleep.gtk, gd->gtk,
		       sm->gtk_wnm_sleep.gtk_len) == 0)) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"WPA: Not reinstalling already in-use GTK to the driver (keyidx=%d tx=%d len=%d)",
			gd->keyidx, gd->tx, gd->gtk_len);
		return 0;
	}

	wpa_hexdump_key(MSG_DEBUG, "WPA: Group Key", gd->gtk, gd->gtk_len);
	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
		"WPA: Installing GTK to the driver (keyidx=%d tx=%d len=%d)",
		gd->keyidx, gd->tx, gd->gtk_len);
	wpa_hexdump(MSG_DEBUG, "WPA: RSC", key_rsc, gd->key_rsc_len);
	if (sm->group_cipher == WPA_CIPHER_TKIP) {
		/* Swap Tx/Rx keys for Michael MIC */
		os_memcpy(gtk_buf, gd->gtk, 16);
		os_memcpy(gtk_buf + 16, gd->gtk + 24, 8);
		os_memcpy(gtk_buf + 24, gd->gtk + 16, 8);
		_gtk = gtk_buf;
	}
	if (sm->pairwise_cipher == WPA_CIPHER_NONE) {
		if (wpa_sm_set_key(sm, gd->alg, NULL,
				   gd->keyidx, 1, key_rsc, gd->key_rsc_len,
				   _gtk, gd->gtk_len) < 0) {
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: Failed to set GTK to the driver "
				"(Group only)");
			os_memset(gtk_buf, 0, sizeof(gtk_buf));
			return -1;
		}
	} else if (wpa_sm_set_key(sm, gd->alg, broadcast_ether_addr,
				  gd->keyidx, gd->tx, key_rsc, gd->key_rsc_len,
				  _gtk, gd->gtk_len) < 0) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: Failed to set GTK to "
			"the driver (alg=%d keylen=%d keyidx=%d)",
			gd->alg, gd->gtk_len, gd->keyidx);
		os_memset(gtk_buf, 0, sizeof(gtk_buf));
		return -1;
	}
	os_memset(gtk_buf, 0, sizeof(gtk_buf));

	if (wnm_sleep) {
		sm->gtk_wnm_sleep.gtk_len = gd->gtk_len;
		os_memcpy(sm->gtk_wnm_sleep.gtk, gd->gtk,
			  sm->gtk_wnm_sleep.gtk_len);
	} else {
		sm->gtk.gtk_len = gd->gtk_len;
		os_memcpy(sm->gtk.gtk, gd->gtk, sm->gtk.gtk_len);
	}

	return 0;
}


static int wpa_supplicant_gtk_tx_bit_workaround(const struct wpa_sm *sm,
						int tx)
{
	if (tx && sm->pairwise_cipher != WPA_CIPHER_NONE) {
		/* Ignore Tx bit for GTK if a pairwise key is used. One AP
		 * seemed to set this bit (incorrectly, since Tx is only when
		 * doing Group Key only APs) and without this workaround, the
		 * data connection does not work because wpa_supplicant
		 * configured non-zero keyidx to be used for unicast. */
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
			"WPA: Tx bit set for GTK, but pairwise "
			"keys are used - ignore Tx bit");
		return 0;
	}
	return tx;
}


static int wpa_supplicant_rsc_relaxation(const struct wpa_sm *sm,
					 const u8 *rsc)
{
	int rsclen;

	if (!sm->wpa_rsc_relaxation)
		return 0;

	rsclen = wpa_cipher_rsc_len(sm->group_cipher);

	/*
	 * Try to detect RSC (endian) corruption issue where the AP sends
	 * the RSC bytes in EAPOL-Key message in the wrong order, both if
	 * it's actually a 6-byte field (as it should be) and if it treats
	 * it as an 8-byte field.
	 * An AP model known to have this bug is the Sapido RB-1632.
	 */
	if (rsclen == 6 && ((rsc[5] && !rsc[0]) || rsc[6] || rsc[7])) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"RSC %02x%02x%02x%02x%02x%02x%02x%02x is likely bogus, using 0",
			rsc[0], rsc[1], rsc[2], rsc[3],
			rsc[4], rsc[5], rsc[6], rsc[7]);

		return 1;
	}

	return 0;
}


static int wpa_supplicant_pairwise_gtk(struct wpa_sm *sm,
				       const struct wpa_eapol_key *key,
				       const u8 *gtk, size_t gtk_len,
				       int key_info)
{
	struct wpa_gtk_data gd;
	const u8 *key_rsc;

	/*
	 * IEEE Std 802.11i-2004 - 8.5.2 EAPOL-Key frames - Figure 43x
	 * GTK KDE format:
	 * KeyID[bits 0-1], Tx [bit 2], Reserved [bits 3-7]
	 * Reserved [bits 0-7]
	 * GTK
	 */

	os_memset(&gd, 0, sizeof(gd));
	wpa_hexdump_key(MSG_DEBUG, "RSN: received GTK in pairwise handshake",
			gtk, gtk_len);

	if (gtk_len < 2 || gtk_len - 2 > sizeof(gd.gtk))
		return -1;

	gd.keyidx = gtk[0] & 0x3;
	gd.tx = wpa_supplicant_gtk_tx_bit_workaround(sm,
						     !!(gtk[0] & BIT(2)));
	gtk += 2;
	gtk_len -= 2;

	os_memcpy(gd.gtk, gtk, gtk_len);
	gd.gtk_len = gtk_len;

	key_rsc = key->key_rsc;
	if (wpa_supplicant_rsc_relaxation(sm, key->key_rsc))
		key_rsc = null_rsc;

	if (sm->group_cipher != WPA_CIPHER_GTK_NOT_USED &&
	    (wpa_supplicant_check_group_cipher(sm, sm->group_cipher,
					       gtk_len, gtk_len,
					       &gd.key_rsc_len, &gd.alg) ||
	     wpa_supplicant_install_gtk(sm, &gd, key_rsc, 0))) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"RSN: Failed to install GTK");
		os_memset(&gd, 0, sizeof(gd));
		return -1;
	}
	os_memset(&gd, 0, sizeof(gd));

	wpa_supplicant_key_neg_complete(sm, sm->bssid,
					key_info & WPA_KEY_INFO_SECURE);
	return 0;
}


#ifdef CONFIG_IEEE80211W
static int wpa_supplicant_install_igtk(struct wpa_sm *sm,
				       const struct wpa_igtk_kde *igtk,
				       int wnm_sleep)
{
	size_t len = wpa_cipher_key_len(sm->mgmt_group_cipher);
	u16 keyidx = WPA_GET_LE16(igtk->keyid);

	/* Detect possible key reinstallation */
	if ((sm->igtk.igtk_len == len &&
	     os_memcmp(sm->igtk.igtk, igtk->igtk, sm->igtk.igtk_len) == 0) ||
	    (sm->igtk_wnm_sleep.igtk_len == len &&
	     os_memcmp(sm->igtk_wnm_sleep.igtk, igtk->igtk,
		       sm->igtk_wnm_sleep.igtk_len) == 0)) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"WPA: Not reinstalling already in-use IGTK to the driver (keyidx=%d)",
			keyidx);
		return  0;
	}

	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
		"WPA: IGTK keyid %d pn " COMPACT_MACSTR,
		keyidx, MAC2STR(igtk->pn));
	wpa_hexdump_key(MSG_DEBUG, "WPA: IGTK", igtk->igtk, len);
	if (keyidx > 4095) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: Invalid IGTK KeyID %d", keyidx);
		return -1;
	}
	if (wpa_sm_set_key(sm, wpa_cipher_to_alg(sm->mgmt_group_cipher),
			   broadcast_ether_addr,
			   keyidx, 0, igtk->pn, sizeof(igtk->pn),
			   igtk->igtk, len) < 0) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: Failed to configure IGTK to the driver");
		return -1;
	}

	if (wnm_sleep) {
		sm->igtk_wnm_sleep.igtk_len = len;
		os_memcpy(sm->igtk_wnm_sleep.igtk, igtk->igtk,
			  sm->igtk_wnm_sleep.igtk_len);
	} else {
		sm->igtk.igtk_len = len;
		os_memcpy(sm->igtk.igtk, igtk->igtk, sm->igtk.igtk_len);
	}

	return 0;
}
#endif /* CONFIG_IEEE80211W */


static int ieee80211w_set_keys(struct wpa_sm *sm,
			       struct wpa_eapol_ie_parse *ie)
{
#ifdef CONFIG_IEEE80211W
	if (!wpa_cipher_valid_mgmt_group(sm->mgmt_group_cipher))
		return 0;

	if (ie->igtk) {
		size_t len;
		const struct wpa_igtk_kde *igtk;

		len = wpa_cipher_key_len(sm->mgmt_group_cipher);
		if (ie->igtk_len != WPA_IGTK_KDE_PREFIX_LEN + len)
			return -1;

		igtk = (const struct wpa_igtk_kde *) ie->igtk;
		if (wpa_supplicant_install_igtk(sm, igtk, 0) < 0)
			return -1;
	}

	return 0;
#else /* CONFIG_IEEE80211W */
	return 0;
#endif /* CONFIG_IEEE80211W */
}


static void wpa_report_ie_mismatch(struct wpa_sm *sm,
				   const char *reason, const u8 *src_addr,
				   const u8 *wpa_ie, size_t wpa_ie_len,
				   const u8 *rsn_ie, size_t rsn_ie_len)
{
	wpa_msg(sm->ctx->msg_ctx, MSG_WARNING, "WPA: %s (src=" MACSTR ")",
		reason, MAC2STR(src_addr));

	if (sm->ap_wpa_ie) {
		wpa_hexdump(MSG_INFO, "WPA: WPA IE in Beacon/ProbeResp",
			    sm->ap_wpa_ie, sm->ap_wpa_ie_len);
	}
	if (wpa_ie) {
		if (!sm->ap_wpa_ie) {
			wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
				"WPA: No WPA IE in Beacon/ProbeResp");
		}
		wpa_hexdump(MSG_INFO, "WPA: WPA IE in 3/4 msg",
			    wpa_ie, wpa_ie_len);
	}

	if (sm->ap_rsn_ie) {
		wpa_hexdump(MSG_INFO, "WPA: RSN IE in Beacon/ProbeResp",
			    sm->ap_rsn_ie, sm->ap_rsn_ie_len);
	}
	if (rsn_ie) {
		if (!sm->ap_rsn_ie) {
			wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
				"WPA: No RSN IE in Beacon/ProbeResp");
		}
		wpa_hexdump(MSG_INFO, "WPA: RSN IE in 3/4 msg",
			    rsn_ie, rsn_ie_len);
	}

	wpa_sm_deauthenticate(sm, WLAN_REASON_IE_IN_4WAY_DIFFERS);
}


#ifdef CONFIG_IEEE80211R

static int ft_validate_mdie(struct wpa_sm *sm,
			    const unsigned char *src_addr,
			    struct wpa_eapol_ie_parse *ie,
			    const u8 *assoc_resp_mdie)
{
	struct rsn_mdie *mdie;

	mdie = (struct rsn_mdie *) (ie->mdie + 2);
	if (ie->mdie == NULL || ie->mdie_len < 2 + sizeof(*mdie) ||
	    os_memcmp(mdie->mobility_domain, sm->mobility_domain,
		      MOBILITY_DOMAIN_ID_LEN) != 0) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "FT: MDIE in msg 3/4 did "
			"not match with the current mobility domain");
		return -1;
	}

	if (assoc_resp_mdie &&
	    (assoc_resp_mdie[1] != ie->mdie[1] ||
	     os_memcmp(assoc_resp_mdie, ie->mdie, 2 + ie->mdie[1]) != 0)) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "FT: MDIE mismatch");
		wpa_hexdump(MSG_DEBUG, "FT: MDIE in EAPOL-Key msg 3/4",
			    ie->mdie, 2 + ie->mdie[1]);
		wpa_hexdump(MSG_DEBUG, "FT: MDIE in (Re)Association Response",
			    assoc_resp_mdie, 2 + assoc_resp_mdie[1]);
		return -1;
	}

	return 0;
}


static int ft_validate_ftie(struct wpa_sm *sm,
			    const unsigned char *src_addr,
			    struct wpa_eapol_ie_parse *ie,
			    const u8 *assoc_resp_ftie)
{
	if (ie->ftie == NULL) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"FT: No FTIE in EAPOL-Key msg 3/4");
		return -1;
	}

	if (assoc_resp_ftie == NULL)
		return 0;

	if (assoc_resp_ftie[1] != ie->ftie[1] ||
	    os_memcmp(assoc_resp_ftie, ie->ftie, 2 + ie->ftie[1]) != 0) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "FT: FTIE mismatch");
		wpa_hexdump(MSG_DEBUG, "FT: FTIE in EAPOL-Key msg 3/4",
			    ie->ftie, 2 + ie->ftie[1]);
		wpa_hexdump(MSG_DEBUG, "FT: FTIE in (Re)Association Response",
			    assoc_resp_ftie, 2 + assoc_resp_ftie[1]);
		return -1;
	}

	return 0;
}


static int ft_validate_rsnie(struct wpa_sm *sm,
			     const unsigned char *src_addr,
			     struct wpa_eapol_ie_parse *ie)
{
	struct wpa_ie_data rsn;

	if (!ie->rsn_ie)
		return 0;

	/*
	 * Verify that PMKR1Name from EAPOL-Key message 3/4
	 * matches with the value we derived.
	 */
	if (wpa_parse_wpa_ie_rsn(ie->rsn_ie, ie->rsn_ie_len, &rsn) < 0 ||
	    rsn.num_pmkid != 1 || rsn.pmkid == NULL) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "FT: No PMKR1Name in "
			"FT 4-way handshake message 3/4");
		return -1;
	}

	if (os_memcmp_const(rsn.pmkid, sm->pmk_r1_name, WPA_PMK_NAME_LEN) != 0)
	{
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"FT: PMKR1Name mismatch in "
			"FT 4-way handshake message 3/4");
		wpa_hexdump(MSG_DEBUG, "FT: PMKR1Name from Authenticator",
			    rsn.pmkid, WPA_PMK_NAME_LEN);
		wpa_hexdump(MSG_DEBUG, "FT: Derived PMKR1Name",
			    sm->pmk_r1_name, WPA_PMK_NAME_LEN);
		return -1;
	}

	return 0;
}


static int wpa_supplicant_validate_ie_ft(struct wpa_sm *sm,
					 const unsigned char *src_addr,
					 struct wpa_eapol_ie_parse *ie)
{
	const u8 *pos, *end, *mdie = NULL, *ftie = NULL;

	if (sm->assoc_resp_ies) {
		pos = sm->assoc_resp_ies;
		end = pos + sm->assoc_resp_ies_len;
		while (end - pos > 2) {
			if (2 + pos[1] > end - pos)
				break;
			switch (*pos) {
			case WLAN_EID_MOBILITY_DOMAIN:
				mdie = pos;
				break;
			case WLAN_EID_FAST_BSS_TRANSITION:
				ftie = pos;
				break;
			}
			pos += 2 + pos[1];
		}
	}

	if (ft_validate_mdie(sm, src_addr, ie, mdie) < 0 ||
	    ft_validate_ftie(sm, src_addr, ie, ftie) < 0 ||
	    ft_validate_rsnie(sm, src_addr, ie) < 0)
		return -1;

	return 0;
}

#endif /* CONFIG_IEEE80211R */


static int wpa_supplicant_validate_ie(struct wpa_sm *sm,
				      const unsigned char *src_addr,
				      struct wpa_eapol_ie_parse *ie)
{
	if (sm->ap_wpa_ie == NULL && sm->ap_rsn_ie == NULL) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"WPA: No WPA/RSN IE for this AP known. "
			"Trying to get from scan results");
		if (wpa_sm_get_beacon_ie(sm) < 0) {
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: Could not find AP from "
				"the scan results");
		} else {
			wpa_msg(sm->ctx->msg_ctx, MSG_DEBUG,
				"WPA: Found the current AP from "
				"updated scan results");
		}
	}

	if (ie->wpa_ie == NULL && ie->rsn_ie == NULL &&
	    (sm->ap_wpa_ie || sm->ap_rsn_ie)) {
		wpa_report_ie_mismatch(sm, "IE in 3/4 msg does not match "
				       "with IE in Beacon/ProbeResp (no IE?)",
				       src_addr, ie->wpa_ie, ie->wpa_ie_len,
				       ie->rsn_ie, ie->rsn_ie_len);
		return -1;
	}

	if ((ie->wpa_ie && sm->ap_wpa_ie &&
	     (ie->wpa_ie_len != sm->ap_wpa_ie_len ||
	      os_memcmp(ie->wpa_ie, sm->ap_wpa_ie, ie->wpa_ie_len) != 0)) ||
	    (ie->rsn_ie && sm->ap_rsn_ie &&
	     wpa_compare_rsn_ie(wpa_key_mgmt_ft(sm->key_mgmt),
				sm->ap_rsn_ie, sm->ap_rsn_ie_len,
				ie->rsn_ie, ie->rsn_ie_len))) {
		wpa_report_ie_mismatch(sm, "IE in 3/4 msg does not match "
				       "with IE in Beacon/ProbeResp",
				       src_addr, ie->wpa_ie, ie->wpa_ie_len,
				       ie->rsn_ie, ie->rsn_ie_len);
		return -1;
	}

	if (sm->proto == WPA_PROTO_WPA &&
	    ie->rsn_ie && sm->ap_rsn_ie == NULL && sm->rsn_enabled) {
		wpa_report_ie_mismatch(sm, "Possible downgrade attack "
				       "detected - RSN was enabled and RSN IE "
				       "was in msg 3/4, but not in "
				       "Beacon/ProbeResp",
				       src_addr, ie->wpa_ie, ie->wpa_ie_len,
				       ie->rsn_ie, ie->rsn_ie_len);
		return -1;
	}

#ifdef CONFIG_IEEE80211R
	if (wpa_key_mgmt_ft(sm->key_mgmt) &&
	    wpa_supplicant_validate_ie_ft(sm, src_addr, ie) < 0)
		return -1;
#endif /* CONFIG_IEEE80211R */

	return 0;
}


/**
 * wpa_supplicant_send_4_of_4 - Send message 4 of WPA/RSN 4-Way Handshake
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @dst: Destination address for the frame
 * @key: Pointer to the EAPOL-Key frame header
 * @ver: Version bits from EAPOL-Key Key Info
 * @key_info: Key Info
 * @ptk: PTK to use for keyed hash and encryption
 * Returns: >= 0 on success, < 0 on failure
 */
int wpa_supplicant_send_4_of_4(struct wpa_sm *sm, const unsigned char *dst,
			       const struct wpa_eapol_key *key,
			       u16 ver, u16 key_info,
			       struct wpa_ptk *ptk)
{
	size_t mic_len, hdrlen, rlen;
	struct wpa_eapol_key *reply;
	u8 *rbuf, *key_mic;

	mic_len = wpa_mic_len(sm->key_mgmt, sm->pmk_len);
	hdrlen = sizeof(*reply) + mic_len + 2;
	rbuf = wpa_sm_alloc_eapol(sm, IEEE802_1X_TYPE_EAPOL_KEY, NULL,
				  hdrlen, &rlen, (void *) &reply);
	if (rbuf == NULL)
		return -1;

	reply->type = (sm->proto == WPA_PROTO_RSN ||
		       sm->proto == WPA_PROTO_OSEN) ?
		EAPOL_KEY_TYPE_RSN : EAPOL_KEY_TYPE_WPA;
	key_info &= WPA_KEY_INFO_SECURE;
	key_info |= ver | WPA_KEY_INFO_KEY_TYPE;
	if (mic_len)
		key_info |= WPA_KEY_INFO_MIC;
	else
		key_info |= WPA_KEY_INFO_ENCR_KEY_DATA;
	WPA_PUT_BE16(reply->key_info, key_info);
	if (sm->proto == WPA_PROTO_RSN || sm->proto == WPA_PROTO_OSEN)
		WPA_PUT_BE16(reply->key_length, 0);
	else
		os_memcpy(reply->key_length, key->key_length, 2);
	os_memcpy(reply->replay_counter, key->replay_counter,
		  WPA_REPLAY_COUNTER_LEN);

	key_mic = (u8 *) (reply + 1);
	WPA_PUT_BE16(key_mic + mic_len, 0);

	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "WPA: Sending EAPOL-Key 4/4");
	return wpa_eapol_key_send(sm, ptk, ver, dst, ETH_P_EAPOL, rbuf, rlen,
				  key_mic);
}


static void wpa_supplicant_process_3_of_4(struct wpa_sm *sm,
					  const struct wpa_eapol_key *key,
					  u16 ver, const u8 *key_data,
					  size_t key_data_len)
{
	u16 key_info, keylen;
	struct wpa_eapol_ie_parse ie;

	wpa_sm_set_state(sm, WPA_4WAY_HANDSHAKE);
	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "WPA: RX message 3 of 4-Way "
		"Handshake from " MACSTR " (ver=%d)", MAC2STR(sm->bssid), ver);

	key_info = WPA_GET_BE16(key->key_info);

	wpa_hexdump(MSG_DEBUG, "WPA: IE KeyData", key_data, key_data_len);
	if (wpa_supplicant_parse_ies(key_data, key_data_len, &ie) < 0)
		goto failed;
	if (ie.gtk && !(key_info & WPA_KEY_INFO_ENCR_KEY_DATA)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: GTK IE in unencrypted key data");
		goto failed;
	}
#ifdef CONFIG_IEEE80211W
	if (ie.igtk && !(key_info & WPA_KEY_INFO_ENCR_KEY_DATA)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: IGTK KDE in unencrypted key data");
		goto failed;
	}

	if (ie.igtk &&
	    wpa_cipher_valid_mgmt_group(sm->mgmt_group_cipher) &&
	    ie.igtk_len != WPA_IGTK_KDE_PREFIX_LEN +
	    (unsigned int) wpa_cipher_key_len(sm->mgmt_group_cipher)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: Invalid IGTK KDE length %lu",
			(unsigned long) ie.igtk_len);
		goto failed;
	}
#endif /* CONFIG_IEEE80211W */

	if (wpa_supplicant_validate_ie(sm, sm->bssid, &ie) < 0)
		goto failed;

	if (os_memcmp(sm->anonce, key->key_nonce, WPA_NONCE_LEN) != 0) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: ANonce from message 1 of 4-Way Handshake "
			"differs from 3 of 4-Way Handshake - drop packet (src="
			MACSTR ")", MAC2STR(sm->bssid));
		goto failed;
	}

	keylen = WPA_GET_BE16(key->key_length);
	if (keylen != wpa_cipher_key_len(sm->pairwise_cipher)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: Invalid %s key length %d (src=" MACSTR
			")", wpa_cipher_txt(sm->pairwise_cipher), keylen,
			MAC2STR(sm->bssid));
		goto failed;
	}

#ifdef CONFIG_P2P
	if (ie.ip_addr_alloc) {
		os_memcpy(sm->p2p_ip_addr, ie.ip_addr_alloc, 3 * 4);
		wpa_hexdump(MSG_DEBUG, "P2P: IP address info",
			    sm->p2p_ip_addr, sizeof(sm->p2p_ip_addr));
	}
#endif /* CONFIG_P2P */

	if (wpa_supplicant_send_4_of_4(sm, sm->bssid, key, ver, key_info,
				       &sm->ptk) < 0) {
		goto failed;
	}

	/* SNonce was successfully used in msg 3/4, so mark it to be renewed
	 * for the next 4-Way Handshake. If msg 3 is received again, the old
	 * SNonce will still be used to avoid changing PTK. */
	sm->renew_snonce = 1;

	if (key_info & WPA_KEY_INFO_INSTALL) {
		if (wpa_supplicant_install_ptk(sm, key))
			goto failed;
	}

	if (key_info & WPA_KEY_INFO_SECURE) {
		wpa_sm_mlme_setprotection(
			sm, sm->bssid, MLME_SETPROTECTION_PROTECT_TYPE_RX,
			MLME_SETPROTECTION_KEY_TYPE_PAIRWISE);
		eapol_sm_notify_portValid(sm->eapol, TRUE);
	}
	wpa_sm_set_state(sm, WPA_GROUP_HANDSHAKE);

	if (sm->group_cipher == WPA_CIPHER_GTK_NOT_USED) {
		wpa_supplicant_key_neg_complete(sm, sm->bssid,
						key_info & WPA_KEY_INFO_SECURE);
	} else if (ie.gtk &&
	    wpa_supplicant_pairwise_gtk(sm, key,
					ie.gtk, ie.gtk_len, key_info) < 0) {
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
			"RSN: Failed to configure GTK");
		goto failed;
	}

	if (ieee80211w_set_keys(sm, &ie) < 0) {
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
			"RSN: Failed to configure IGTK");
		goto failed;
	}

	if (ie.gtk)
		wpa_sm_set_rekey_offload(sm);

	/* Add PMKSA cache entry for Suite B AKMs here since PMKID can be
	 * calculated only after KCK has been derived. Though, do not replace an
	 * existing PMKSA entry after each 4-way handshake (i.e., new KCK/PMKID)
	 * to avoid unnecessary changes of PMKID while continuing to use the
	 * same PMK. */
	if (sm->proto == WPA_PROTO_RSN && wpa_key_mgmt_suite_b(sm->key_mgmt) &&
	    !sm->cur_pmksa) {
		struct rsn_pmksa_cache_entry *sa;

		sa = pmksa_cache_add(sm->pmksa, sm->pmk, sm->pmk_len, NULL,
				     sm->ptk.kck, sm->ptk.kck_len,
				     sm->bssid, sm->own_addr,
				     sm->network_ctx, sm->key_mgmt, NULL);
		if (!sm->cur_pmksa)
			sm->cur_pmksa = sa;
	}

	sm->msg_3_of_4_ok = 1;
	return;

failed:
	wpa_sm_deauthenticate(sm, WLAN_REASON_UNSPECIFIED);
}


static int wpa_supplicant_process_1_of_2_rsn(struct wpa_sm *sm,
					     const u8 *keydata,
					     size_t keydatalen,
					     u16 key_info,
					     struct wpa_gtk_data *gd)
{
	int maxkeylen;
	struct wpa_eapol_ie_parse ie;

	wpa_hexdump_key(MSG_DEBUG, "RSN: msg 1/2 key data",
			keydata, keydatalen);
	if (wpa_supplicant_parse_ies(keydata, keydatalen, &ie) < 0)
		return -1;
	if (ie.gtk && !(key_info & WPA_KEY_INFO_ENCR_KEY_DATA)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: GTK IE in unencrypted key data");
		return -1;
	}
	if (ie.gtk == NULL) {
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
			"WPA: No GTK IE in Group Key msg 1/2");
		return -1;
	}
	maxkeylen = gd->gtk_len = ie.gtk_len - 2;

	if (wpa_supplicant_check_group_cipher(sm, sm->group_cipher,
					      gd->gtk_len, maxkeylen,
					      &gd->key_rsc_len, &gd->alg))
		return -1;

	wpa_hexdump_key(MSG_DEBUG, "RSN: received GTK in group key handshake",
			ie.gtk, ie.gtk_len);
	gd->keyidx = ie.gtk[0] & 0x3;
	gd->tx = wpa_supplicant_gtk_tx_bit_workaround(sm,
						      !!(ie.gtk[0] & BIT(2)));
	if (ie.gtk_len - 2 > sizeof(gd->gtk)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
			"RSN: Too long GTK in GTK IE (len=%lu)",
			(unsigned long) ie.gtk_len - 2);
		return -1;
	}
	os_memcpy(gd->gtk, ie.gtk + 2, ie.gtk_len - 2);

	if (ieee80211w_set_keys(sm, &ie) < 0)
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
			"RSN: Failed to configure IGTK");

	return 0;
}


static int wpa_supplicant_process_1_of_2_wpa(struct wpa_sm *sm,
					     const struct wpa_eapol_key *key,
					     const u8 *key_data,
					     size_t key_data_len, u16 key_info,
					     u16 ver, struct wpa_gtk_data *gd)
{
	size_t maxkeylen;
	u16 gtk_len;

	gtk_len = WPA_GET_BE16(key->key_length);
	maxkeylen = key_data_len;
	if (ver == WPA_KEY_INFO_TYPE_HMAC_SHA1_AES) {
		if (maxkeylen < 8) {
			wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
				"WPA: Too short maxkeylen (%lu)",
				(unsigned long) maxkeylen);
			return -1;
		}
		maxkeylen -= 8;
	}

	if (gtk_len > maxkeylen ||
	    wpa_supplicant_check_group_cipher(sm, sm->group_cipher,
					      gtk_len, maxkeylen,
					      &gd->key_rsc_len, &gd->alg))
		return -1;

	gd->gtk_len = gtk_len;
	gd->keyidx = (key_info & WPA_KEY_INFO_KEY_INDEX_MASK) >>
		WPA_KEY_INFO_KEY_INDEX_SHIFT;
	if (ver == WPA_KEY_INFO_TYPE_HMAC_MD5_RC4 && sm->ptk.kek_len == 16) {
#ifdef CONFIG_NO_RC4
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: RC4 not supported in the build");
		return -1;
#else /* CONFIG_NO_RC4 */
		u8 ek[32];
		if (key_data_len > sizeof(gd->gtk)) {
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: RC4 key data too long (%lu)",
				(unsigned long) key_data_len);
			return -1;
		}
		os_memcpy(ek, key->key_iv, 16);
		os_memcpy(ek + 16, sm->ptk.kek, sm->ptk.kek_len);
		os_memcpy(gd->gtk, key_data, key_data_len);
		if (rc4_skip(ek, 32, 256, gd->gtk, key_data_len)) {
			os_memset(ek, 0, sizeof(ek));
			wpa_msg(sm->ctx->msg_ctx, MSG_ERROR,
				"WPA: RC4 failed");
			return -1;
		}
		os_memset(ek, 0, sizeof(ek));
#endif /* CONFIG_NO_RC4 */
	} else if (ver == WPA_KEY_INFO_TYPE_HMAC_SHA1_AES) {
		if (maxkeylen % 8) {
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: Unsupported AES-WRAP len %lu",
				(unsigned long) maxkeylen);
			return -1;
		}
		if (maxkeylen > sizeof(gd->gtk)) {
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: AES-WRAP key data "
				"too long (keydatalen=%lu maxkeylen=%lu)",
				(unsigned long) key_data_len,
				(unsigned long) maxkeylen);
			return -1;
		}
		if (aes_unwrap(sm->ptk.kek, sm->ptk.kek_len, maxkeylen / 8,
			       key_data, gd->gtk)) {
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: AES unwrap failed - could not decrypt "
				"GTK");
			return -1;
		}
	} else {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: Unsupported key_info type %d", ver);
		return -1;
	}
	gd->tx = wpa_supplicant_gtk_tx_bit_workaround(
		sm, !!(key_info & WPA_KEY_INFO_TXRX));
	return 0;
}


static int wpa_supplicant_send_2_of_2(struct wpa_sm *sm,
				      const struct wpa_eapol_key *key,
				      int ver, u16 key_info)
{
	size_t mic_len, hdrlen, rlen;
	struct wpa_eapol_key *reply;
	u8 *rbuf, *key_mic;

	mic_len = wpa_mic_len(sm->key_mgmt, sm->pmk_len);
	hdrlen = sizeof(*reply) + mic_len + 2;
	rbuf = wpa_sm_alloc_eapol(sm, IEEE802_1X_TYPE_EAPOL_KEY, NULL,
				  hdrlen, &rlen, (void *) &reply);
	if (rbuf == NULL)
		return -1;

	reply->type = (sm->proto == WPA_PROTO_RSN ||
		       sm->proto == WPA_PROTO_OSEN) ?
		EAPOL_KEY_TYPE_RSN : EAPOL_KEY_TYPE_WPA;
	key_info &= WPA_KEY_INFO_KEY_INDEX_MASK;
	key_info |= ver | WPA_KEY_INFO_SECURE;
	if (mic_len)
		key_info |= WPA_KEY_INFO_MIC;
	else
		key_info |= WPA_KEY_INFO_ENCR_KEY_DATA;
	WPA_PUT_BE16(reply->key_info, key_info);
	if (sm->proto == WPA_PROTO_RSN || sm->proto == WPA_PROTO_OSEN)
		WPA_PUT_BE16(reply->key_length, 0);
	else
		os_memcpy(reply->key_length, key->key_length, 2);
	os_memcpy(reply->replay_counter, key->replay_counter,
		  WPA_REPLAY_COUNTER_LEN);

	key_mic = (u8 *) (reply + 1);
	WPA_PUT_BE16(key_mic + mic_len, 0);

	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "WPA: Sending EAPOL-Key 2/2");
	return wpa_eapol_key_send(sm, &sm->ptk, ver, sm->bssid, ETH_P_EAPOL,
				  rbuf, rlen, key_mic);
}


static void wpa_supplicant_process_1_of_2(struct wpa_sm *sm,
					  const unsigned char *src_addr,
					  const struct wpa_eapol_key *key,
					  const u8 *key_data,
					  size_t key_data_len, u16 ver)
{
	u16 key_info;
	int rekey, ret;
	struct wpa_gtk_data gd;
	const u8 *key_rsc;

	if (!sm->msg_3_of_4_ok && !wpa_fils_is_completed(sm)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
			"WPA: Group Key Handshake started prior to completion of 4-way handshake");
		goto failed;
	}

	os_memset(&gd, 0, sizeof(gd));

	rekey = wpa_sm_get_state(sm) == WPA_COMPLETED;
	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "WPA: RX message 1 of Group Key "
		"Handshake from " MACSTR " (ver=%d)", MAC2STR(src_addr), ver);

	key_info = WPA_GET_BE16(key->key_info);

	if (sm->proto == WPA_PROTO_RSN || sm->proto == WPA_PROTO_OSEN) {
		ret = wpa_supplicant_process_1_of_2_rsn(sm, key_data,
							key_data_len, key_info,
							&gd);
	} else {
		ret = wpa_supplicant_process_1_of_2_wpa(sm, key, key_data,
							key_data_len,
							key_info, ver, &gd);
	}

	wpa_sm_set_state(sm, WPA_GROUP_HANDSHAKE);

	if (ret)
		goto failed;

	key_rsc = key->key_rsc;
	if (wpa_supplicant_rsc_relaxation(sm, key->key_rsc))
		key_rsc = null_rsc;

	if (wpa_supplicant_install_gtk(sm, &gd, key_rsc, 0) ||
	    wpa_supplicant_send_2_of_2(sm, key, ver, key_info) < 0)
		goto failed;
	os_memset(&gd, 0, sizeof(gd));

	if (rekey) {
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO, "WPA: Group rekeying "
			"completed with " MACSTR " [GTK=%s]",
			MAC2STR(sm->bssid), wpa_cipher_txt(sm->group_cipher));
		wpa_sm_cancel_auth_timeout(sm);
		wpa_sm_set_state(sm, WPA_COMPLETED);
	} else {
		wpa_supplicant_key_neg_complete(sm, sm->bssid,
						key_info &
						WPA_KEY_INFO_SECURE);
	}

	wpa_sm_set_rekey_offload(sm);

	return;

failed:
	os_memset(&gd, 0, sizeof(gd));
	wpa_sm_deauthenticate(sm, WLAN_REASON_UNSPECIFIED);
}


static int wpa_supplicant_verify_eapol_key_mic(struct wpa_sm *sm,
					       struct wpa_eapol_key *key,
					       u16 ver,
					       const u8 *buf, size_t len)
{
	u8 mic[WPA_EAPOL_KEY_MIC_MAX_LEN];
	int ok = 0;
	size_t mic_len = wpa_mic_len(sm->key_mgmt, sm->pmk_len);

	os_memcpy(mic, key + 1, mic_len);
	if (sm->tptk_set) {
		os_memset(key + 1, 0, mic_len);
		if (wpa_eapol_key_mic(sm->tptk.kck, sm->tptk.kck_len,
				      sm->key_mgmt,
				      ver, buf, len, (u8 *) (key + 1)) < 0 ||
		    os_memcmp_const(mic, key + 1, mic_len) != 0) {
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: Invalid EAPOL-Key MIC "
				"when using TPTK - ignoring TPTK");
		} else {
			ok = 1;
			sm->tptk_set = 0;
			sm->ptk_set = 1;
			os_memcpy(&sm->ptk, &sm->tptk, sizeof(sm->ptk));
			os_memset(&sm->tptk, 0, sizeof(sm->tptk));
			/*
			 * This assures the same TPTK in sm->tptk can never be
			 * copied twice to sm->ptk as the new PTK. In
			 * combination with the installed flag in the wpa_ptk
			 * struct, this assures the same PTK is only installed
			 * once.
			 */
			sm->renew_snonce = 1;
		}
	}

	if (!ok && sm->ptk_set) {
		os_memset(key + 1, 0, mic_len);
		if (wpa_eapol_key_mic(sm->ptk.kck, sm->ptk.kck_len,
				      sm->key_mgmt,
				      ver, buf, len, (u8 *) (key + 1)) < 0 ||
		    os_memcmp_const(mic, key + 1, mic_len) != 0) {
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: Invalid EAPOL-Key MIC - "
				"dropping packet");
			return -1;
		}
		ok = 1;
	}

	if (!ok) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: Could not verify EAPOL-Key MIC - "
			"dropping packet");
		return -1;
	}

	os_memcpy(sm->rx_replay_counter, key->replay_counter,
		  WPA_REPLAY_COUNTER_LEN);
	sm->rx_replay_counter_set = 1;
	return 0;
}


/* Decrypt RSN EAPOL-Key key data (RC4 or AES-WRAP) */
static int wpa_supplicant_decrypt_key_data(struct wpa_sm *sm,
					   struct wpa_eapol_key *key,
					   size_t mic_len, u16 ver,
					   u8 *key_data, size_t *key_data_len)
{
	wpa_hexdump(MSG_DEBUG, "RSN: encrypted key data",
		    key_data, *key_data_len);
	if (!sm->ptk_set) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: PTK not available, cannot decrypt EAPOL-Key Key "
			"Data");
		return -1;
	}

	/* Decrypt key data here so that this operation does not need
	 * to be implemented separately for each message type. */
	if (ver == WPA_KEY_INFO_TYPE_HMAC_MD5_RC4 && sm->ptk.kek_len == 16) {
#ifdef CONFIG_NO_RC4
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: RC4 not supported in the build");
		return -1;
#else /* CONFIG_NO_RC4 */
		u8 ek[32];

		wpa_printf(MSG_DEBUG, "WPA: Decrypt Key Data using RC4");
		os_memcpy(ek, key->key_iv, 16);
		os_memcpy(ek + 16, sm->ptk.kek, sm->ptk.kek_len);
		if (rc4_skip(ek, 32, 256, key_data, *key_data_len)) {
			os_memset(ek, 0, sizeof(ek));
			wpa_msg(sm->ctx->msg_ctx, MSG_ERROR,
				"WPA: RC4 failed");
			return -1;
		}
		os_memset(ek, 0, sizeof(ek));
#endif /* CONFIG_NO_RC4 */
	} else if (ver == WPA_KEY_INFO_TYPE_HMAC_SHA1_AES ||
		   ver == WPA_KEY_INFO_TYPE_AES_128_CMAC ||
		   wpa_use_aes_key_wrap(sm->key_mgmt)) {
		u8 *buf;

		wpa_printf(MSG_DEBUG,
			   "WPA: Decrypt Key Data using AES-UNWRAP (KEK length %u)",
			   (unsigned int) sm->ptk.kek_len);
		if (*key_data_len < 8 || *key_data_len % 8) {
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: Unsupported AES-WRAP len %u",
				(unsigned int) *key_data_len);
			return -1;
		}
		*key_data_len -= 8; /* AES-WRAP adds 8 bytes */
		buf = os_malloc(*key_data_len);
		if (buf == NULL) {
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: No memory for AES-UNWRAP buffer");
			return -1;
		}
		if (aes_unwrap(sm->ptk.kek, sm->ptk.kek_len, *key_data_len / 8,
			       key_data, buf)) {
			bin_clear_free(buf, *key_data_len);
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: AES unwrap failed - "
				"could not decrypt EAPOL-Key key data");
			return -1;
		}
		os_memcpy(key_data, buf, *key_data_len);
		bin_clear_free(buf, *key_data_len);
		WPA_PUT_BE16(((u8 *) (key + 1)) + mic_len, *key_data_len);
	} else {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: Unsupported key_info type %d", ver);
		return -1;
	}
	wpa_hexdump_key(MSG_DEBUG, "WPA: decrypted EAPOL-Key key data",
			key_data, *key_data_len);
	return 0;
}


/**
 * wpa_sm_aborted_cached - Notify WPA that PMKSA caching was aborted
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 */
void wpa_sm_aborted_cached(struct wpa_sm *sm)
{
	if (sm && sm->cur_pmksa) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"RSN: Cancelling PMKSA caching attempt");
		sm->cur_pmksa = NULL;
	}
}


static void wpa_eapol_key_dump(struct wpa_sm *sm,
			       const struct wpa_eapol_key *key,
			       unsigned int key_data_len,
			       const u8 *mic, unsigned int mic_len)
{
#ifndef CONFIG_NO_STDOUT_DEBUG
	u16 key_info = WPA_GET_BE16(key->key_info);

	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "  EAPOL-Key type=%d", key->type);
	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
		"  key_info 0x%x (ver=%d keyidx=%d rsvd=%d %s%s%s%s%s%s%s%s)",
		key_info, key_info & WPA_KEY_INFO_TYPE_MASK,
		(key_info & WPA_KEY_INFO_KEY_INDEX_MASK) >>
		WPA_KEY_INFO_KEY_INDEX_SHIFT,
		(key_info & (BIT(13) | BIT(14) | BIT(15))) >> 13,
		key_info & WPA_KEY_INFO_KEY_TYPE ? "Pairwise" : "Group",
		key_info & WPA_KEY_INFO_INSTALL ? " Install" : "",
		key_info & WPA_KEY_INFO_ACK ? " Ack" : "",
		key_info & WPA_KEY_INFO_MIC ? " MIC" : "",
		key_info & WPA_KEY_INFO_SECURE ? " Secure" : "",
		key_info & WPA_KEY_INFO_ERROR ? " Error" : "",
		key_info & WPA_KEY_INFO_REQUEST ? " Request" : "",
		key_info & WPA_KEY_INFO_ENCR_KEY_DATA ? " Encr" : "");
	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
		"  key_length=%u key_data_length=%u",
		WPA_GET_BE16(key->key_length), key_data_len);
	wpa_hexdump(MSG_DEBUG, "  replay_counter",
		    key->replay_counter, WPA_REPLAY_COUNTER_LEN);
	wpa_hexdump(MSG_DEBUG, "  key_nonce", key->key_nonce, WPA_NONCE_LEN);
	wpa_hexdump(MSG_DEBUG, "  key_iv", key->key_iv, 16);
	wpa_hexdump(MSG_DEBUG, "  key_rsc", key->key_rsc, 8);
	wpa_hexdump(MSG_DEBUG, "  key_id (reserved)", key->key_id, 8);
	wpa_hexdump(MSG_DEBUG, "  key_mic", mic, mic_len);
#endif /* CONFIG_NO_STDOUT_DEBUG */
}


#ifdef CONFIG_FILS
static int wpa_supp_aead_decrypt(struct wpa_sm *sm, u8 *buf, size_t buf_len,
				 size_t *key_data_len)
{
	struct wpa_ptk *ptk;
	struct ieee802_1x_hdr *hdr;
	struct wpa_eapol_key *key;
	u8 *pos, *tmp;
	const u8 *aad[1];
	size_t aad_len[1];

	if (*key_data_len < AES_BLOCK_SIZE) {
		wpa_printf(MSG_INFO, "No room for AES-SIV data in the frame");
		return -1;
	}

	if (sm->tptk_set)
		ptk = &sm->tptk;
	else if (sm->ptk_set)
		ptk = &sm->ptk;
	else
		return -1;

	hdr = (struct ieee802_1x_hdr *) buf;
	key = (struct wpa_eapol_key *) (hdr + 1);
	pos = (u8 *) (key + 1);
	pos += 2; /* Pointing at the Encrypted Key Data field */

	tmp = os_malloc(*key_data_len);
	if (!tmp)
		return -1;

	/* AES-SIV AAD from EAPOL protocol version field (inclusive) to
	 * to Key Data (exclusive). */
	aad[0] = buf;
	aad_len[0] = pos - buf;
	if (aes_siv_decrypt(ptk->kek, ptk->kek_len, pos, *key_data_len,
			    1, aad, aad_len, tmp) < 0) {
		wpa_printf(MSG_INFO, "Invalid AES-SIV data in the frame");
		bin_clear_free(tmp, *key_data_len);
		return -1;
	}

	/* AEAD decryption and validation completed successfully */
	(*key_data_len) -= AES_BLOCK_SIZE;
	wpa_hexdump_key(MSG_DEBUG, "WPA: Decrypted Key Data",
			tmp, *key_data_len);

	/* Replace Key Data field with the decrypted version */
	os_memcpy(pos, tmp, *key_data_len);
	pos -= 2; /* Key Data Length field */
	WPA_PUT_BE16(pos, *key_data_len);
	bin_clear_free(tmp, *key_data_len);

	if (sm->tptk_set) {
		sm->tptk_set = 0;
		sm->ptk_set = 1;
		os_memcpy(&sm->ptk, &sm->tptk, sizeof(sm->ptk));
		os_memset(&sm->tptk, 0, sizeof(sm->tptk));
	}

	os_memcpy(sm->rx_replay_counter, key->replay_counter,
		  WPA_REPLAY_COUNTER_LEN);
	sm->rx_replay_counter_set = 1;

	return 0;
}
#endif /* CONFIG_FILS */


/**
 * wpa_sm_rx_eapol - Process received WPA EAPOL frames
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @src_addr: Source MAC address of the EAPOL packet
 * @buf: Pointer to the beginning of the EAPOL data (EAPOL header)
 * @len: Length of the EAPOL frame
 * Returns: 1 = WPA EAPOL-Key processed, 0 = not a WPA EAPOL-Key, -1 failure
 *
 * This function is called for each received EAPOL frame. Other than EAPOL-Key
 * frames can be skipped if filtering is done elsewhere. wpa_sm_rx_eapol() is
 * only processing WPA and WPA2 EAPOL-Key frames.
 *
 * The received EAPOL-Key packets are validated and valid packets are replied
 * to. In addition, key material (PTK, GTK) is configured at the end of a
 * successful key handshake.
 */
int wpa_sm_rx_eapol(struct wpa_sm *sm, const u8 *src_addr,
		    const u8 *buf, size_t len)
{
	size_t plen, data_len, key_data_len;
	const struct ieee802_1x_hdr *hdr;
	struct wpa_eapol_key *key;
	u16 key_info, ver;
	u8 *tmp = NULL;
	int ret = -1;
	u8 *mic, *key_data;
	size_t mic_len, keyhdrlen;

#ifdef CONFIG_IEEE80211R
	sm->ft_completed = 0;
#endif /* CONFIG_IEEE80211R */

	mic_len = wpa_mic_len(sm->key_mgmt, sm->pmk_len);
	keyhdrlen = sizeof(*key) + mic_len + 2;

	if (len < sizeof(*hdr) + keyhdrlen) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"WPA: EAPOL frame too short to be a WPA "
			"EAPOL-Key (len %lu, expecting at least %lu)",
			(unsigned long) len,
			(unsigned long) sizeof(*hdr) + keyhdrlen);
		return 0;
	}

	hdr = (const struct ieee802_1x_hdr *) buf;
	plen = be_to_host16(hdr->length);
	data_len = plen + sizeof(*hdr);
	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
		"IEEE 802.1X RX: version=%d type=%d length=%lu",
		hdr->version, hdr->type, (unsigned long) plen);

	if (hdr->version < EAPOL_VERSION) {
		/* TODO: backwards compatibility */
	}
	if (hdr->type != IEEE802_1X_TYPE_EAPOL_KEY) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"WPA: EAPOL frame (type %u) discarded, "
			"not a Key frame", hdr->type);
		ret = 0;
		goto out;
	}
	wpa_hexdump(MSG_MSGDUMP, "WPA: RX EAPOL-Key", buf, len);
	if (plen > len - sizeof(*hdr) || plen < keyhdrlen) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"WPA: EAPOL frame payload size %lu "
			"invalid (frame size %lu)",
			(unsigned long) plen, (unsigned long) len);
		ret = 0;
		goto out;
	}
	if (data_len < len) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"WPA: ignoring %lu bytes after the IEEE 802.1X data",
			(unsigned long) len - data_len);
	}

	/*
	 * Make a copy of the frame since we need to modify the buffer during
	 * MAC validation and Key Data decryption.
	 */
	tmp = os_memdup(buf, data_len);
	if (tmp == NULL)
		goto out;
	key = (struct wpa_eapol_key *) (tmp + sizeof(struct ieee802_1x_hdr));
	mic = (u8 *) (key + 1);
	key_data = mic + mic_len + 2;

	if (key->type != EAPOL_KEY_TYPE_WPA && key->type != EAPOL_KEY_TYPE_RSN)
	{
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"WPA: EAPOL-Key type (%d) unknown, discarded",
			key->type);
		ret = 0;
		goto out;
	}

	key_data_len = WPA_GET_BE16(mic + mic_len);
	wpa_eapol_key_dump(sm, key, key_data_len, mic, mic_len);

	if (key_data_len > plen - keyhdrlen) {
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO, "WPA: Invalid EAPOL-Key "
			"frame - key_data overflow (%u > %u)",
			(unsigned int) key_data_len,
			(unsigned int) (plen - keyhdrlen));
		goto out;
	}

	eapol_sm_notify_lower_layer_success(sm->eapol, 0);
	key_info = WPA_GET_BE16(key->key_info);
	ver = key_info & WPA_KEY_INFO_TYPE_MASK;
	if (ver != WPA_KEY_INFO_TYPE_HMAC_MD5_RC4 &&
#if defined(CONFIG_IEEE80211R) || defined(CONFIG_IEEE80211W)
	    ver != WPA_KEY_INFO_TYPE_AES_128_CMAC &&
#endif /* CONFIG_IEEE80211R || CONFIG_IEEE80211W */
	    ver != WPA_KEY_INFO_TYPE_HMAC_SHA1_AES &&
	    !wpa_use_akm_defined(sm->key_mgmt)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
			"WPA: Unsupported EAPOL-Key descriptor version %d",
			ver);
		goto out;
	}

	if (wpa_use_akm_defined(sm->key_mgmt) &&
	    ver != WPA_KEY_INFO_TYPE_AKM_DEFINED) {
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
			"RSN: Unsupported EAPOL-Key descriptor version %d (expected AKM defined = 0)",
			ver);
		goto out;
	}

#ifdef CONFIG_IEEE80211R
	if (wpa_key_mgmt_ft(sm->key_mgmt)) {
		/* IEEE 802.11r uses a new key_info type (AES-128-CMAC). */
		if (ver != WPA_KEY_INFO_TYPE_AES_128_CMAC &&
		    !wpa_use_akm_defined(sm->key_mgmt)) {
			wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
				"FT: AP did not use AES-128-CMAC");
			goto out;
		}
	} else
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_IEEE80211W
	if (wpa_key_mgmt_sha256(sm->key_mgmt)) {
		if (ver != WPA_KEY_INFO_TYPE_AES_128_CMAC &&
		    !wpa_use_akm_defined(sm->key_mgmt)) {
			wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
				"WPA: AP did not use the "
				"negotiated AES-128-CMAC");
			goto out;
		}
	} else
#endif /* CONFIG_IEEE80211W */
	if (sm->pairwise_cipher == WPA_CIPHER_CCMP &&
	    !wpa_use_akm_defined(sm->key_mgmt) &&
	    ver != WPA_KEY_INFO_TYPE_HMAC_SHA1_AES) {
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
			"WPA: CCMP is used, but EAPOL-Key "
			"descriptor version (%d) is not 2", ver);
		if (sm->group_cipher != WPA_CIPHER_CCMP &&
		    !(key_info & WPA_KEY_INFO_KEY_TYPE)) {
			/* Earlier versions of IEEE 802.11i did not explicitly
			 * require version 2 descriptor for all EAPOL-Key
			 * packets, so allow group keys to use version 1 if
			 * CCMP is not used for them. */
			wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
				"WPA: Backwards compatibility: allow invalid "
				"version for non-CCMP group keys");
		} else if (ver == WPA_KEY_INFO_TYPE_AES_128_CMAC) {
			wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
				"WPA: Interoperability workaround: allow incorrect (should have been HMAC-SHA1), but stronger (is AES-128-CMAC), descriptor version to be used");
		} else
			goto out;
	} else if (sm->pairwise_cipher == WPA_CIPHER_GCMP &&
		   !wpa_use_akm_defined(sm->key_mgmt) &&
		   ver != WPA_KEY_INFO_TYPE_HMAC_SHA1_AES) {
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
			"WPA: GCMP is used, but EAPOL-Key "
			"descriptor version (%d) is not 2", ver);
		goto out;
	}

	if (sm->rx_replay_counter_set &&
	    os_memcmp(key->replay_counter, sm->rx_replay_counter,
		      WPA_REPLAY_COUNTER_LEN) <= 0) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"WPA: EAPOL-Key Replay Counter did not increase - "
			"dropping packet");
		goto out;
	}

	if (key_info & WPA_KEY_INFO_SMK_MESSAGE) {
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
			"WPA: Unsupported SMK bit in key_info");
		goto out;
	}

	if (!(key_info & WPA_KEY_INFO_ACK)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
			"WPA: No Ack bit in key_info");
		goto out;
	}

	if (key_info & WPA_KEY_INFO_REQUEST) {
		wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
			"WPA: EAPOL-Key with Request bit - dropped");
		goto out;
	}

	if ((key_info & WPA_KEY_INFO_MIC) &&
	    wpa_supplicant_verify_eapol_key_mic(sm, key, ver, tmp, data_len))
		goto out;

#ifdef CONFIG_FILS
	if (!mic_len && (key_info & WPA_KEY_INFO_ENCR_KEY_DATA)) {
		if (wpa_supp_aead_decrypt(sm, tmp, data_len, &key_data_len))
			goto out;
	}
#endif /* CONFIG_FILS */

	if ((sm->proto == WPA_PROTO_RSN || sm->proto == WPA_PROTO_OSEN) &&
	    (key_info & WPA_KEY_INFO_ENCR_KEY_DATA) && mic_len) {
		/*
		 * Only decrypt the Key Data field if the frame's authenticity
		 * was verified. When using AES-SIV (FILS), the MIC flag is not
		 * set, so this check should only be performed if mic_len != 0
		 * which is the case in this code branch.
		 */
		if (!(key_info & WPA_KEY_INFO_MIC)) {
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: Ignore EAPOL-Key with encrypted but unauthenticated data");
			goto out;
		}
		if (wpa_supplicant_decrypt_key_data(sm, key, mic_len,
						    ver, key_data,
						    &key_data_len))
			goto out;
	}

	if (key_info & WPA_KEY_INFO_KEY_TYPE) {
		if (key_info & WPA_KEY_INFO_KEY_INDEX_MASK) {
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: Ignored EAPOL-Key (Pairwise) with "
				"non-zero key index");
			goto out;
		}
		if (key_info & (WPA_KEY_INFO_MIC |
				WPA_KEY_INFO_ENCR_KEY_DATA)) {
			/* 3/4 4-Way Handshake */
			wpa_supplicant_process_3_of_4(sm, key, ver, key_data,
						      key_data_len);
		} else {
			/* 1/4 4-Way Handshake */
			wpa_supplicant_process_1_of_4(sm, src_addr, key,
						      ver, key_data,
						      key_data_len);
		}
	} else {
		if ((mic_len && (key_info & WPA_KEY_INFO_MIC)) ||
		    (!mic_len && (key_info & WPA_KEY_INFO_ENCR_KEY_DATA))) {
			/* 1/2 Group Key Handshake */
			wpa_supplicant_process_1_of_2(sm, src_addr, key,
						      key_data, key_data_len,
						      ver);
		} else {
			wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
				"WPA: EAPOL-Key (Group) without Mic/Encr bit - "
				"dropped");
		}
	}

	ret = 1;

out:
	bin_clear_free(tmp, data_len);
	return ret;
}


#ifdef CONFIG_CTRL_IFACE
static u32 wpa_key_mgmt_suite(struct wpa_sm *sm)
{
	switch (sm->key_mgmt) {
	case WPA_KEY_MGMT_IEEE8021X:
		return ((sm->proto == WPA_PROTO_RSN ||
			 sm->proto == WPA_PROTO_OSEN) ?
			RSN_AUTH_KEY_MGMT_UNSPEC_802_1X :
			WPA_AUTH_KEY_MGMT_UNSPEC_802_1X);
	case WPA_KEY_MGMT_PSK:
		return (sm->proto == WPA_PROTO_RSN ?
			RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X :
			WPA_AUTH_KEY_MGMT_PSK_OVER_802_1X);
#ifdef CONFIG_IEEE80211R
	case WPA_KEY_MGMT_FT_IEEE8021X:
		return RSN_AUTH_KEY_MGMT_FT_802_1X;
	case WPA_KEY_MGMT_FT_PSK:
		return RSN_AUTH_KEY_MGMT_FT_PSK;
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_IEEE80211W
	case WPA_KEY_MGMT_IEEE8021X_SHA256:
		return RSN_AUTH_KEY_MGMT_802_1X_SHA256;
	case WPA_KEY_MGMT_PSK_SHA256:
		return RSN_AUTH_KEY_MGMT_PSK_SHA256;
#endif /* CONFIG_IEEE80211W */
	case WPA_KEY_MGMT_CCKM:
		return (sm->proto == WPA_PROTO_RSN ?
			RSN_AUTH_KEY_MGMT_CCKM:
			WPA_AUTH_KEY_MGMT_CCKM);
	case WPA_KEY_MGMT_WPA_NONE:
		return WPA_AUTH_KEY_MGMT_NONE;
	case WPA_KEY_MGMT_IEEE8021X_SUITE_B:
		return RSN_AUTH_KEY_MGMT_802_1X_SUITE_B;
	case WPA_KEY_MGMT_IEEE8021X_SUITE_B_192:
		return RSN_AUTH_KEY_MGMT_802_1X_SUITE_B_192;
	default:
		return 0;
	}
}


#define RSN_SUITE "%02x-%02x-%02x-%d"
#define RSN_SUITE_ARG(s) \
((s) >> 24) & 0xff, ((s) >> 16) & 0xff, ((s) >> 8) & 0xff, (s) & 0xff

/**
 * wpa_sm_get_mib - Dump text list of MIB entries
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @buf: Buffer for the list
 * @buflen: Length of the buffer
 * Returns: Number of bytes written to buffer
 *
 * This function is used fetch dot11 MIB variables.
 */
int wpa_sm_get_mib(struct wpa_sm *sm, char *buf, size_t buflen)
{
	char pmkid_txt[PMKID_LEN * 2 + 1];
	int rsna, ret;
	size_t len;

	if (sm->cur_pmksa) {
		wpa_snprintf_hex(pmkid_txt, sizeof(pmkid_txt),
				 sm->cur_pmksa->pmkid, PMKID_LEN);
	} else
		pmkid_txt[0] = '\0';

	if ((wpa_key_mgmt_wpa_psk(sm->key_mgmt) ||
	     wpa_key_mgmt_wpa_ieee8021x(sm->key_mgmt)) &&
	    sm->proto == WPA_PROTO_RSN)
		rsna = 1;
	else
		rsna = 0;

	ret = os_snprintf(buf, buflen,
			  "dot11RSNAOptionImplemented=TRUE\n"
			  "dot11RSNAPreauthenticationImplemented=TRUE\n"
			  "dot11RSNAEnabled=%s\n"
			  "dot11RSNAPreauthenticationEnabled=%s\n"
			  "dot11RSNAConfigVersion=%d\n"
			  "dot11RSNAConfigPairwiseKeysSupported=5\n"
			  "dot11RSNAConfigGroupCipherSize=%d\n"
			  "dot11RSNAConfigPMKLifetime=%d\n"
			  "dot11RSNAConfigPMKReauthThreshold=%d\n"
			  "dot11RSNAConfigNumberOfPTKSAReplayCounters=1\n"
			  "dot11RSNAConfigSATimeout=%d\n",
			  rsna ? "TRUE" : "FALSE",
			  rsna ? "TRUE" : "FALSE",
			  RSN_VERSION,
			  wpa_cipher_key_len(sm->group_cipher) * 8,
			  sm->dot11RSNAConfigPMKLifetime,
			  sm->dot11RSNAConfigPMKReauthThreshold,
			  sm->dot11RSNAConfigSATimeout);
	if (os_snprintf_error(buflen, ret))
		return 0;
	len = ret;

	ret = os_snprintf(
		buf + len, buflen - len,
		"dot11RSNAAuthenticationSuiteSelected=" RSN_SUITE "\n"
		"dot11RSNAPairwiseCipherSelected=" RSN_SUITE "\n"
		"dot11RSNAGroupCipherSelected=" RSN_SUITE "\n"
		"dot11RSNAPMKIDUsed=%s\n"
		"dot11RSNAAuthenticationSuiteRequested=" RSN_SUITE "\n"
		"dot11RSNAPairwiseCipherRequested=" RSN_SUITE "\n"
		"dot11RSNAGroupCipherRequested=" RSN_SUITE "\n"
		"dot11RSNAConfigNumberOfGTKSAReplayCounters=0\n"
		"dot11RSNA4WayHandshakeFailures=%u\n",
		RSN_SUITE_ARG(wpa_key_mgmt_suite(sm)),
		RSN_SUITE_ARG(wpa_cipher_to_suite(sm->proto,
						  sm->pairwise_cipher)),
		RSN_SUITE_ARG(wpa_cipher_to_suite(sm->proto,
						  sm->group_cipher)),
		pmkid_txt,
		RSN_SUITE_ARG(wpa_key_mgmt_suite(sm)),
		RSN_SUITE_ARG(wpa_cipher_to_suite(sm->proto,
						  sm->pairwise_cipher)),
		RSN_SUITE_ARG(wpa_cipher_to_suite(sm->proto,
						  sm->group_cipher)),
		sm->dot11RSNA4WayHandshakeFailures);
	if (!os_snprintf_error(buflen - len, ret))
		len += ret;

	return (int) len;
}
#endif /* CONFIG_CTRL_IFACE */


static void wpa_sm_pmksa_free_cb(struct rsn_pmksa_cache_entry *entry,
				 void *ctx, enum pmksa_free_reason reason)
{
	struct wpa_sm *sm = ctx;
	int deauth = 0;

	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "RSN: PMKSA cache entry free_cb: "
		MACSTR " reason=%d", MAC2STR(entry->aa), reason);

	if (sm->cur_pmksa == entry) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"RSN: %s current PMKSA entry",
			reason == PMKSA_REPLACE ? "replaced" : "removed");
		pmksa_cache_clear_current(sm);

		/*
		 * If an entry is simply being replaced, there's no need to
		 * deauthenticate because it will be immediately re-added.
		 * This happens when EAP authentication is completed again
		 * (reauth or failed PMKSA caching attempt).
		 */
		if (reason != PMKSA_REPLACE)
			deauth = 1;
	}

	if (reason == PMKSA_EXPIRE &&
	    (sm->pmk_len == entry->pmk_len &&
	     os_memcmp(sm->pmk, entry->pmk, sm->pmk_len) == 0)) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"RSN: deauthenticating due to expired PMK");
		pmksa_cache_clear_current(sm);
		deauth = 1;
	}

	if (deauth) {
		sm->pmk_len = 0;
		os_memset(sm->pmk, 0, sizeof(sm->pmk));
		wpa_sm_deauthenticate(sm, WLAN_REASON_UNSPECIFIED);
	}
}


/**
 * wpa_sm_init - Initialize WPA state machine
 * @ctx: Context pointer for callbacks; this needs to be an allocated buffer
 * Returns: Pointer to the allocated WPA state machine data
 *
 * This function is used to allocate a new WPA state machine and the returned
 * value is passed to all WPA state machine calls.
 */
struct wpa_sm * wpa_sm_init(struct wpa_sm_ctx *ctx)
{
	struct wpa_sm *sm;

	sm = os_zalloc(sizeof(*sm));
	if (sm == NULL)
		return NULL;
	dl_list_init(&sm->pmksa_candidates);
	sm->renew_snonce = 1;
	sm->ctx = ctx;

	sm->dot11RSNAConfigPMKLifetime = 43200;
	sm->dot11RSNAConfigPMKReauthThreshold = 70;
	sm->dot11RSNAConfigSATimeout = 60;

	sm->pmksa = pmksa_cache_init(wpa_sm_pmksa_free_cb, sm, sm);
	if (sm->pmksa == NULL) {
		wpa_msg(sm->ctx->msg_ctx, MSG_ERROR,
			"RSN: PMKSA cache initialization failed");
		os_free(sm);
		return NULL;
	}

	return sm;
}


/**
 * wpa_sm_deinit - Deinitialize WPA state machine
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 */
void wpa_sm_deinit(struct wpa_sm *sm)
{
	if (sm == NULL)
		return;
	pmksa_cache_deinit(sm->pmksa);
	eloop_cancel_timeout(wpa_sm_start_preauth, sm, NULL);
	eloop_cancel_timeout(wpa_sm_rekey_ptk, sm, NULL);
	os_free(sm->assoc_wpa_ie);
	os_free(sm->ap_wpa_ie);
	os_free(sm->ap_rsn_ie);
	wpa_sm_drop_sa(sm);
	os_free(sm->ctx);
#ifdef CONFIG_IEEE80211R
	os_free(sm->assoc_resp_ies);
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_TESTING_OPTIONS
	wpabuf_free(sm->test_assoc_ie);
#endif /* CONFIG_TESTING_OPTIONS */
#ifdef CONFIG_FILS_SK_PFS
	crypto_ecdh_deinit(sm->fils_ecdh);
#endif /* CONFIG_FILS_SK_PFS */
#ifdef CONFIG_FILS
	wpabuf_free(sm->fils_ft_ies);
#endif /* CONFIG_FILS */
#ifdef CONFIG_OWE
	crypto_ecdh_deinit(sm->owe_ecdh);
#endif /* CONFIG_OWE */
	os_free(sm);
}


/**
 * wpa_sm_notify_assoc - Notify WPA state machine about association
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @bssid: The BSSID of the new association
 *
 * This function is called to let WPA state machine know that the connection
 * was established.
 */
void wpa_sm_notify_assoc(struct wpa_sm *sm, const u8 *bssid)
{
	int clear_keys = 1;

	if (sm == NULL)
		return;

	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
		"WPA: Association event - clear replay counter");
	os_memcpy(sm->bssid, bssid, ETH_ALEN);
	os_memset(sm->rx_replay_counter, 0, WPA_REPLAY_COUNTER_LEN);
	sm->rx_replay_counter_set = 0;
	sm->renew_snonce = 1;
	if (os_memcmp(sm->preauth_bssid, bssid, ETH_ALEN) == 0)
		rsn_preauth_deinit(sm);

#ifdef CONFIG_IEEE80211R
	if (wpa_ft_is_completed(sm)) {
		/*
		 * Clear portValid to kick EAPOL state machine to re-enter
		 * AUTHENTICATED state to get the EAPOL port Authorized.
		 */
		eapol_sm_notify_portValid(sm->eapol, FALSE);
		wpa_supplicant_key_neg_complete(sm, sm->bssid, 1);

		/* Prepare for the next transition */
		wpa_ft_prepare_auth_request(sm, NULL);

		clear_keys = 0;
	}
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_FILS
	if (sm->fils_completed) {
		/*
		 * Clear portValid to kick EAPOL state machine to re-enter
		 * AUTHENTICATED state to get the EAPOL port Authorized.
		 */
		wpa_supplicant_key_neg_complete(sm, sm->bssid, 1);
		clear_keys = 0;
	}
#endif /* CONFIG_FILS */

	if (clear_keys) {
		/*
		 * IEEE 802.11, 8.4.10: Delete PTK SA on (re)association if
		 * this is not part of a Fast BSS Transition.
		 */
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "WPA: Clear old PTK");
		sm->ptk_set = 0;
		os_memset(&sm->ptk, 0, sizeof(sm->ptk));
		sm->tptk_set = 0;
		os_memset(&sm->tptk, 0, sizeof(sm->tptk));
		os_memset(&sm->gtk, 0, sizeof(sm->gtk));
		os_memset(&sm->gtk_wnm_sleep, 0, sizeof(sm->gtk_wnm_sleep));
#ifdef CONFIG_IEEE80211W
		os_memset(&sm->igtk, 0, sizeof(sm->igtk));
		os_memset(&sm->igtk_wnm_sleep, 0, sizeof(sm->igtk_wnm_sleep));
#endif /* CONFIG_IEEE80211W */
	}

#ifdef CONFIG_TDLS
	wpa_tdls_assoc(sm);
#endif /* CONFIG_TDLS */

#ifdef CONFIG_P2P
	os_memset(sm->p2p_ip_addr, 0, sizeof(sm->p2p_ip_addr));
#endif /* CONFIG_P2P */
}


/**
 * wpa_sm_notify_disassoc - Notify WPA state machine about disassociation
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 *
 * This function is called to let WPA state machine know that the connection
 * was lost. This will abort any existing pre-authentication session.
 */
void wpa_sm_notify_disassoc(struct wpa_sm *sm)
{
	eloop_cancel_timeout(wpa_sm_start_preauth, sm, NULL);
	eloop_cancel_timeout(wpa_sm_rekey_ptk, sm, NULL);
	rsn_preauth_deinit(sm);
	pmksa_cache_clear_current(sm);
	if (wpa_sm_get_state(sm) == WPA_4WAY_HANDSHAKE)
		sm->dot11RSNA4WayHandshakeFailures++;
#ifdef CONFIG_TDLS
	wpa_tdls_disassoc(sm);
#endif /* CONFIG_TDLS */
#ifdef CONFIG_FILS
	sm->fils_completed = 0;
#endif /* CONFIG_FILS */
#ifdef CONFIG_IEEE80211R
	sm->ft_reassoc_completed = 0;
#endif /* CONFIG_IEEE80211R */

	/* Keys are not needed in the WPA state machine anymore */
	wpa_sm_drop_sa(sm);

	sm->msg_3_of_4_ok = 0;
	os_memset(sm->bssid, 0, ETH_ALEN);
}


/**
 * wpa_sm_set_pmk - Set PMK
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @pmk: The new PMK
 * @pmk_len: The length of the new PMK in bytes
 * @pmkid: Calculated PMKID
 * @bssid: AA to add into PMKSA cache or %NULL to not cache the PMK
 *
 * Configure the PMK for WPA state machine.
 */
void wpa_sm_set_pmk(struct wpa_sm *sm, const u8 *pmk, size_t pmk_len,
		    const u8 *pmkid, const u8 *bssid)
{
	if (sm == NULL)
		return;

	wpa_hexdump_key(MSG_DEBUG, "WPA: Set PMK based on external data",
			pmk, pmk_len);
	sm->pmk_len = pmk_len;
	os_memcpy(sm->pmk, pmk, pmk_len);

#ifdef CONFIG_IEEE80211R
	/* Set XXKey to be PSK for FT key derivation */
	sm->xxkey_len = pmk_len;
	os_memcpy(sm->xxkey, pmk, pmk_len);
#endif /* CONFIG_IEEE80211R */

	if (bssid) {
		pmksa_cache_add(sm->pmksa, pmk, pmk_len, pmkid, NULL, 0,
				bssid, sm->own_addr,
				sm->network_ctx, sm->key_mgmt, NULL);
	}
}


/**
 * wpa_sm_set_pmk_from_pmksa - Set PMK based on the current PMKSA
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 *
 * Take the PMK from the current PMKSA into use. If no PMKSA is active, the PMK
 * will be cleared.
 */
void wpa_sm_set_pmk_from_pmksa(struct wpa_sm *sm)
{
	if (sm == NULL)
		return;

	if (sm->cur_pmksa) {
		wpa_hexdump_key(MSG_DEBUG,
				"WPA: Set PMK based on current PMKSA",
				sm->cur_pmksa->pmk, sm->cur_pmksa->pmk_len);
		sm->pmk_len = sm->cur_pmksa->pmk_len;
		os_memcpy(sm->pmk, sm->cur_pmksa->pmk, sm->pmk_len);
	} else {
		wpa_printf(MSG_DEBUG, "WPA: No current PMKSA - clear PMK");
		sm->pmk_len = 0;
		os_memset(sm->pmk, 0, PMK_LEN_MAX);
	}
}


/**
 * wpa_sm_set_fast_reauth - Set fast reauthentication (EAP) enabled/disabled
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @fast_reauth: Whether fast reauthentication (EAP) is allowed
 */
void wpa_sm_set_fast_reauth(struct wpa_sm *sm, int fast_reauth)
{
	if (sm)
		sm->fast_reauth = fast_reauth;
}


/**
 * wpa_sm_set_scard_ctx - Set context pointer for smartcard callbacks
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @scard_ctx: Context pointer for smartcard related callback functions
 */
void wpa_sm_set_scard_ctx(struct wpa_sm *sm, void *scard_ctx)
{
	if (sm == NULL)
		return;
	sm->scard_ctx = scard_ctx;
	if (sm->preauth_eapol)
		eapol_sm_register_scard_ctx(sm->preauth_eapol, scard_ctx);
}


/**
 * wpa_sm_set_config - Notification of current configration change
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @config: Pointer to current network configuration
 *
 * Notify WPA state machine that configuration has changed. config will be
 * stored as a backpointer to network configuration. This can be %NULL to clear
 * the stored pointed.
 */
void wpa_sm_set_config(struct wpa_sm *sm, struct rsn_supp_config *config)
{
	if (!sm)
		return;

	if (config) {
		sm->network_ctx = config->network_ctx;
		sm->allowed_pairwise_cipher = config->allowed_pairwise_cipher;
		sm->proactive_key_caching = config->proactive_key_caching;
		sm->eap_workaround = config->eap_workaround;
		sm->eap_conf_ctx = config->eap_conf_ctx;
		if (config->ssid) {
			os_memcpy(sm->ssid, config->ssid, config->ssid_len);
			sm->ssid_len = config->ssid_len;
		} else
			sm->ssid_len = 0;
		sm->wpa_ptk_rekey = config->wpa_ptk_rekey;
		sm->p2p = config->p2p;
		sm->wpa_rsc_relaxation = config->wpa_rsc_relaxation;
#ifdef CONFIG_FILS
		if (config->fils_cache_id) {
			sm->fils_cache_id_set = 1;
			os_memcpy(sm->fils_cache_id, config->fils_cache_id,
				  FILS_CACHE_ID_LEN);
		} else {
			sm->fils_cache_id_set = 0;
		}
#endif /* CONFIG_FILS */
	} else {
		sm->network_ctx = NULL;
		sm->allowed_pairwise_cipher = 0;
		sm->proactive_key_caching = 0;
		sm->eap_workaround = 0;
		sm->eap_conf_ctx = NULL;
		sm->ssid_len = 0;
		sm->wpa_ptk_rekey = 0;
		sm->p2p = 0;
		sm->wpa_rsc_relaxation = 0;
	}
}


/**
 * wpa_sm_set_own_addr - Set own MAC address
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @addr: Own MAC address
 */
void wpa_sm_set_own_addr(struct wpa_sm *sm, const u8 *addr)
{
	if (sm)
		os_memcpy(sm->own_addr, addr, ETH_ALEN);
}


/**
 * wpa_sm_set_ifname - Set network interface name
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @ifname: Interface name
 * @bridge_ifname: Optional bridge interface name (for pre-auth)
 */
void wpa_sm_set_ifname(struct wpa_sm *sm, const char *ifname,
		       const char *bridge_ifname)
{
	if (sm) {
		sm->ifname = ifname;
		sm->bridge_ifname = bridge_ifname;
	}
}


/**
 * wpa_sm_set_eapol - Set EAPOL state machine pointer
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @eapol: Pointer to EAPOL state machine allocated with eapol_sm_init()
 */
void wpa_sm_set_eapol(struct wpa_sm *sm, struct eapol_sm *eapol)
{
	if (sm)
		sm->eapol = eapol;
}


/**
 * wpa_sm_set_param - Set WPA state machine parameters
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @param: Parameter field
 * @value: Parameter value
 * Returns: 0 on success, -1 on failure
 */
int wpa_sm_set_param(struct wpa_sm *sm, enum wpa_sm_conf_params param,
		     unsigned int value)
{
	int ret = 0;

	if (sm == NULL)
		return -1;

	switch (param) {
	case RSNA_PMK_LIFETIME:
		if (value > 0)
			sm->dot11RSNAConfigPMKLifetime = value;
		else
			ret = -1;
		break;
	case RSNA_PMK_REAUTH_THRESHOLD:
		if (value > 0 && value <= 100)
			sm->dot11RSNAConfigPMKReauthThreshold = value;
		else
			ret = -1;
		break;
	case RSNA_SA_TIMEOUT:
		if (value > 0)
			sm->dot11RSNAConfigSATimeout = value;
		else
			ret = -1;
		break;
	case WPA_PARAM_PROTO:
		sm->proto = value;
		break;
	case WPA_PARAM_PAIRWISE:
		sm->pairwise_cipher = value;
		break;
	case WPA_PARAM_GROUP:
		sm->group_cipher = value;
		break;
	case WPA_PARAM_KEY_MGMT:
		sm->key_mgmt = value;
		break;
#ifdef CONFIG_IEEE80211W
	case WPA_PARAM_MGMT_GROUP:
		sm->mgmt_group_cipher = value;
		break;
#endif /* CONFIG_IEEE80211W */
	case WPA_PARAM_RSN_ENABLED:
		sm->rsn_enabled = value;
		break;
	case WPA_PARAM_MFP:
		sm->mfp = value;
		break;
	default:
		break;
	}

	return ret;
}


/**
 * wpa_sm_get_status - Get WPA state machine
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @buf: Buffer for status information
 * @buflen: Maximum buffer length
 * @verbose: Whether to include verbose status information
 * Returns: Number of bytes written to buf.
 *
 * Query WPA state machine for status information. This function fills in
 * a text area with current status information. If the buffer (buf) is not
 * large enough, status information will be truncated to fit the buffer.
 */
int wpa_sm_get_status(struct wpa_sm *sm, char *buf, size_t buflen,
		      int verbose)
{
	char *pos = buf, *end = buf + buflen;
	int ret;

	ret = os_snprintf(pos, end - pos,
			  "pairwise_cipher=%s\n"
			  "group_cipher=%s\n"
			  "key_mgmt=%s\n",
			  wpa_cipher_txt(sm->pairwise_cipher),
			  wpa_cipher_txt(sm->group_cipher),
			  wpa_key_mgmt_txt(sm->key_mgmt, sm->proto));
	if (os_snprintf_error(end - pos, ret))
		return pos - buf;
	pos += ret;

	if (sm->mfp != NO_MGMT_FRAME_PROTECTION && sm->ap_rsn_ie) {
		struct wpa_ie_data rsn;
		if (wpa_parse_wpa_ie_rsn(sm->ap_rsn_ie, sm->ap_rsn_ie_len, &rsn)
		    >= 0 &&
		    rsn.capabilities & (WPA_CAPABILITY_MFPR |
					WPA_CAPABILITY_MFPC)) {
			ret = os_snprintf(pos, end - pos, "pmf=%d\n"
					  "mgmt_group_cipher=%s\n",
					  (rsn.capabilities &
					   WPA_CAPABILITY_MFPR) ? 2 : 1,
					  wpa_cipher_txt(
						  sm->mgmt_group_cipher));
			if (os_snprintf_error(end - pos, ret))
				return pos - buf;
			pos += ret;
		}
	}

	return pos - buf;
}


int wpa_sm_pmf_enabled(struct wpa_sm *sm)
{
	struct wpa_ie_data rsn;

	if (sm->mfp == NO_MGMT_FRAME_PROTECTION || !sm->ap_rsn_ie)
		return 0;

	if (wpa_parse_wpa_ie_rsn(sm->ap_rsn_ie, sm->ap_rsn_ie_len, &rsn) >= 0 &&
	    rsn.capabilities & (WPA_CAPABILITY_MFPR | WPA_CAPABILITY_MFPC))
		return 1;

	return 0;
}


/**
 * wpa_sm_set_assoc_wpa_ie_default - Generate own WPA/RSN IE from configuration
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @wpa_ie: Pointer to buffer for WPA/RSN IE
 * @wpa_ie_len: Pointer to the length of the wpa_ie buffer
 * Returns: 0 on success, -1 on failure
 */
int wpa_sm_set_assoc_wpa_ie_default(struct wpa_sm *sm, u8 *wpa_ie,
				    size_t *wpa_ie_len)
{
	int res;

	if (sm == NULL)
		return -1;

#ifdef CONFIG_TESTING_OPTIONS
	if (sm->test_assoc_ie) {
		wpa_printf(MSG_DEBUG,
			   "TESTING: Replace association WPA/RSN IE");
		if (*wpa_ie_len < wpabuf_len(sm->test_assoc_ie))
			return -1;
		os_memcpy(wpa_ie, wpabuf_head(sm->test_assoc_ie),
			  wpabuf_len(sm->test_assoc_ie));
		res = wpabuf_len(sm->test_assoc_ie);
	} else
#endif /* CONFIG_TESTING_OPTIONS */
	res = wpa_gen_wpa_ie(sm, wpa_ie, *wpa_ie_len);
	if (res < 0)
		return -1;
	*wpa_ie_len = res;

	wpa_hexdump(MSG_DEBUG, "WPA: Set own WPA IE default",
		    wpa_ie, *wpa_ie_len);

	if (sm->assoc_wpa_ie == NULL) {
		/*
		 * Make a copy of the WPA/RSN IE so that 4-Way Handshake gets
		 * the correct version of the IE even if PMKSA caching is
		 * aborted (which would remove PMKID from IE generation).
		 */
		sm->assoc_wpa_ie = os_memdup(wpa_ie, *wpa_ie_len);
		if (sm->assoc_wpa_ie == NULL)
			return -1;

		sm->assoc_wpa_ie_len = *wpa_ie_len;
	} else {
		wpa_hexdump(MSG_DEBUG,
			    "WPA: Leave previously set WPA IE default",
			    sm->assoc_wpa_ie, sm->assoc_wpa_ie_len);
	}

	return 0;
}


/**
 * wpa_sm_set_assoc_wpa_ie - Set own WPA/RSN IE from (Re)AssocReq
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @ie: Pointer to IE data (starting from id)
 * @len: IE length
 * Returns: 0 on success, -1 on failure
 *
 * Inform WPA state machine about the WPA/RSN IE used in (Re)Association
 * Request frame. The IE will be used to override the default value generated
 * with wpa_sm_set_assoc_wpa_ie_default().
 */
int wpa_sm_set_assoc_wpa_ie(struct wpa_sm *sm, const u8 *ie, size_t len)
{
	if (sm == NULL)
		return -1;

	os_free(sm->assoc_wpa_ie);
	if (ie == NULL || len == 0) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"WPA: clearing own WPA/RSN IE");
		sm->assoc_wpa_ie = NULL;
		sm->assoc_wpa_ie_len = 0;
	} else {
		wpa_hexdump(MSG_DEBUG, "WPA: set own WPA/RSN IE", ie, len);
		sm->assoc_wpa_ie = os_memdup(ie, len);
		if (sm->assoc_wpa_ie == NULL)
			return -1;

		sm->assoc_wpa_ie_len = len;
	}

	return 0;
}


/**
 * wpa_sm_set_ap_wpa_ie - Set AP WPA IE from Beacon/ProbeResp
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @ie: Pointer to IE data (starting from id)
 * @len: IE length
 * Returns: 0 on success, -1 on failure
 *
 * Inform WPA state machine about the WPA IE used in Beacon / Probe Response
 * frame.
 */
int wpa_sm_set_ap_wpa_ie(struct wpa_sm *sm, const u8 *ie, size_t len)
{
	if (sm == NULL)
		return -1;

	os_free(sm->ap_wpa_ie);
	if (ie == NULL || len == 0) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"WPA: clearing AP WPA IE");
		sm->ap_wpa_ie = NULL;
		sm->ap_wpa_ie_len = 0;
	} else {
		wpa_hexdump(MSG_DEBUG, "WPA: set AP WPA IE", ie, len);
		sm->ap_wpa_ie = os_memdup(ie, len);
		if (sm->ap_wpa_ie == NULL)
			return -1;

		sm->ap_wpa_ie_len = len;
	}

	return 0;
}


/**
 * wpa_sm_set_ap_rsn_ie - Set AP RSN IE from Beacon/ProbeResp
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @ie: Pointer to IE data (starting from id)
 * @len: IE length
 * Returns: 0 on success, -1 on failure
 *
 * Inform WPA state machine about the RSN IE used in Beacon / Probe Response
 * frame.
 */
int wpa_sm_set_ap_rsn_ie(struct wpa_sm *sm, const u8 *ie, size_t len)
{
	if (sm == NULL)
		return -1;

	os_free(sm->ap_rsn_ie);
	if (ie == NULL || len == 0) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"WPA: clearing AP RSN IE");
		sm->ap_rsn_ie = NULL;
		sm->ap_rsn_ie_len = 0;
	} else {
		wpa_hexdump(MSG_DEBUG, "WPA: set AP RSN IE", ie, len);
		sm->ap_rsn_ie = os_memdup(ie, len);
		if (sm->ap_rsn_ie == NULL)
			return -1;

		sm->ap_rsn_ie_len = len;
	}

	return 0;
}


/**
 * wpa_sm_parse_own_wpa_ie - Parse own WPA/RSN IE
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @data: Pointer to data area for parsing results
 * Returns: 0 on success, -1 if IE is not known, or -2 on parsing failure
 *
 * Parse the contents of the own WPA or RSN IE from (Re)AssocReq and write the
 * parsed data into data.
 */
int wpa_sm_parse_own_wpa_ie(struct wpa_sm *sm, struct wpa_ie_data *data)
{
	if (sm == NULL)
		return -1;

	if (sm->assoc_wpa_ie == NULL) {
		wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG,
			"WPA: No WPA/RSN IE available from association info");
		return -1;
	}
	if (wpa_parse_wpa_ie(sm->assoc_wpa_ie, sm->assoc_wpa_ie_len, data))
		return -2;
	return 0;
}


int wpa_sm_pmksa_cache_list(struct wpa_sm *sm, char *buf, size_t len)
{
	return pmksa_cache_list(sm->pmksa, buf, len);
}


struct rsn_pmksa_cache_entry * wpa_sm_pmksa_cache_head(struct wpa_sm *sm)
{
	return pmksa_cache_head(sm->pmksa);
}


struct rsn_pmksa_cache_entry *
wpa_sm_pmksa_cache_add_entry(struct wpa_sm *sm,
			     struct rsn_pmksa_cache_entry * entry)
{
	return pmksa_cache_add_entry(sm->pmksa, entry);
}


void wpa_sm_pmksa_cache_add(struct wpa_sm *sm, const u8 *pmk, size_t pmk_len,
			    const u8 *pmkid, const u8 *bssid,
			    const u8 *fils_cache_id)
{
	sm->cur_pmksa = pmksa_cache_add(sm->pmksa, pmk, pmk_len, pmkid, NULL, 0,
					bssid, sm->own_addr, sm->network_ctx,
					sm->key_mgmt, fils_cache_id);
}


int wpa_sm_pmksa_exists(struct wpa_sm *sm, const u8 *bssid,
			const void *network_ctx)
{
	return pmksa_cache_get(sm->pmksa, bssid, NULL, network_ctx, 0) != NULL;
}


void wpa_sm_drop_sa(struct wpa_sm *sm)
{
	wpa_dbg(sm->ctx->msg_ctx, MSG_DEBUG, "WPA: Clear old PMK and PTK");
	sm->ptk_set = 0;
	sm->tptk_set = 0;
	sm->pmk_len = 0;
	os_memset(sm->pmk, 0, sizeof(sm->pmk));
	os_memset(&sm->ptk, 0, sizeof(sm->ptk));
	os_memset(&sm->tptk, 0, sizeof(sm->tptk));
	os_memset(&sm->gtk, 0, sizeof(sm->gtk));
	os_memset(&sm->gtk_wnm_sleep, 0, sizeof(sm->gtk_wnm_sleep));
#ifdef CONFIG_IEEE80211W
	os_memset(&sm->igtk, 0, sizeof(sm->igtk));
	os_memset(&sm->igtk_wnm_sleep, 0, sizeof(sm->igtk_wnm_sleep));
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_IEEE80211R
	os_memset(sm->xxkey, 0, sizeof(sm->xxkey));
	sm->xxkey_len = 0;
	os_memset(sm->pmk_r0, 0, sizeof(sm->pmk_r0));
	sm->pmk_r0_len = 0;
	os_memset(sm->pmk_r1, 0, sizeof(sm->pmk_r1));
	sm->pmk_r1_len = 0;
#endif /* CONFIG_IEEE80211R */
}


int wpa_sm_has_ptk(struct wpa_sm *sm)
{
	if (sm == NULL)
		return 0;
	return sm->ptk_set;
}


void wpa_sm_update_replay_ctr(struct wpa_sm *sm, const u8 *replay_ctr)
{
	os_memcpy(sm->rx_replay_counter, replay_ctr, WPA_REPLAY_COUNTER_LEN);
}


void wpa_sm_pmksa_cache_flush(struct wpa_sm *sm, void *network_ctx)
{
	pmksa_cache_flush(sm->pmksa, network_ctx, NULL, 0);
}


#ifdef CONFIG_WNM
int wpa_wnmsleep_install_key(struct wpa_sm *sm, u8 subelem_id, u8 *buf)
{
	u16 keyinfo;
	u8 keylen;  /* plaintext key len */
	u8 *key_rsc;

	if (subelem_id == WNM_SLEEP_SUBELEM_GTK) {
		struct wpa_gtk_data gd;

		os_memset(&gd, 0, sizeof(gd));
		keylen = wpa_cipher_key_len(sm->group_cipher);
		gd.key_rsc_len = wpa_cipher_rsc_len(sm->group_cipher);
		gd.alg = wpa_cipher_to_alg(sm->group_cipher);
		if (gd.alg == WPA_ALG_NONE) {
			wpa_printf(MSG_DEBUG, "Unsupported group cipher suite");
			return -1;
		}

		key_rsc = buf + 5;
		keyinfo = WPA_GET_LE16(buf + 2);
		gd.gtk_len = keylen;
		if (gd.gtk_len != buf[4]) {
			wpa_printf(MSG_DEBUG, "GTK len mismatch len %d vs %d",
				   gd.gtk_len, buf[4]);
			return -1;
		}
		gd.keyidx = keyinfo & 0x03; /* B0 - B1 */
		gd.tx = wpa_supplicant_gtk_tx_bit_workaround(
		         sm, !!(keyinfo & WPA_KEY_INFO_TXRX));

		os_memcpy(gd.gtk, buf + 13, gd.gtk_len);

		wpa_hexdump_key(MSG_DEBUG, "Install GTK (WNM SLEEP)",
				gd.gtk, gd.gtk_len);
		if (wpa_supplicant_install_gtk(sm, &gd, key_rsc, 1)) {
			os_memset(&gd, 0, sizeof(gd));
			wpa_printf(MSG_DEBUG, "Failed to install the GTK in "
				   "WNM mode");
			return -1;
		}
		os_memset(&gd, 0, sizeof(gd));
#ifdef CONFIG_IEEE80211W
	} else if (subelem_id == WNM_SLEEP_SUBELEM_IGTK) {
		const struct wpa_igtk_kde *igtk;

		igtk = (const struct wpa_igtk_kde *) (buf + 2);
		if (wpa_supplicant_install_igtk(sm, igtk, 1) < 0)
			return -1;
#endif /* CONFIG_IEEE80211W */
	} else {
		wpa_printf(MSG_DEBUG, "Unknown element id");
		return -1;
	}

	return 0;
}
#endif /* CONFIG_WNM */


#ifdef CONFIG_P2P

int wpa_sm_get_p2p_ip_addr(struct wpa_sm *sm, u8 *buf)
{
	if (sm == NULL || WPA_GET_BE32(sm->p2p_ip_addr) == 0)
		return -1;
	os_memcpy(buf, sm->p2p_ip_addr, 3 * 4);
	return 0;
}

#endif /* CONFIG_P2P */


void wpa_sm_set_rx_replay_ctr(struct wpa_sm *sm, const u8 *rx_replay_counter)
{
	if (rx_replay_counter == NULL)
		return;

	os_memcpy(sm->rx_replay_counter, rx_replay_counter,
		  WPA_REPLAY_COUNTER_LEN);
	sm->rx_replay_counter_set = 1;
	wpa_printf(MSG_DEBUG, "Updated key replay counter");
}


void wpa_sm_set_ptk_kck_kek(struct wpa_sm *sm,
			    const u8 *ptk_kck, size_t ptk_kck_len,
			    const u8 *ptk_kek, size_t ptk_kek_len)
{
	if (ptk_kck && ptk_kck_len <= WPA_KCK_MAX_LEN) {
		os_memcpy(sm->ptk.kck, ptk_kck, ptk_kck_len);
		sm->ptk.kck_len = ptk_kck_len;
		wpa_printf(MSG_DEBUG, "Updated PTK KCK");
	}
	if (ptk_kek && ptk_kek_len <= WPA_KEK_MAX_LEN) {
		os_memcpy(sm->ptk.kek, ptk_kek, ptk_kek_len);
		sm->ptk.kek_len = ptk_kek_len;
		wpa_printf(MSG_DEBUG, "Updated PTK KEK");
	}
	sm->ptk_set = 1;
}


#ifdef CONFIG_TESTING_OPTIONS

void wpa_sm_set_test_assoc_ie(struct wpa_sm *sm, struct wpabuf *buf)
{
	wpabuf_free(sm->test_assoc_ie);
	sm->test_assoc_ie = buf;
}


const u8 * wpa_sm_get_anonce(struct wpa_sm *sm)
{
	return sm->anonce;
}

#endif /* CONFIG_TESTING_OPTIONS */


unsigned int wpa_sm_get_key_mgmt(struct wpa_sm *sm)
{
	return sm->key_mgmt;
}


#ifdef CONFIG_FILS

struct wpabuf * fils_build_auth(struct wpa_sm *sm, int dh_group, const u8 *md)
{
	struct wpabuf *buf = NULL;
	struct wpabuf *erp_msg;
	struct wpabuf *pub = NULL;

	erp_msg = eapol_sm_build_erp_reauth_start(sm->eapol);
	if (!erp_msg && !sm->cur_pmksa) {
		wpa_printf(MSG_DEBUG,
			   "FILS: Neither ERP EAP-Initiate/Re-auth nor PMKSA cache entry is available - skip FILS");
		goto fail;
	}

	wpa_printf(MSG_DEBUG, "FILS: Try to use FILS (erp=%d pmksa_cache=%d)",
		   erp_msg != NULL, sm->cur_pmksa != NULL);

	sm->fils_completed = 0;

	if (!sm->assoc_wpa_ie) {
		wpa_printf(MSG_INFO, "FILS: No own RSN IE set for FILS");
		goto fail;
	}

	if (random_get_bytes(sm->fils_nonce, FILS_NONCE_LEN) < 0 ||
	    random_get_bytes(sm->fils_session, FILS_SESSION_LEN) < 0)
		goto fail;

	wpa_hexdump(MSG_DEBUG, "FILS: Generated FILS Nonce",
		    sm->fils_nonce, FILS_NONCE_LEN);
	wpa_hexdump(MSG_DEBUG, "FILS: Generated FILS Session",
		    sm->fils_session, FILS_SESSION_LEN);

#ifdef CONFIG_FILS_SK_PFS
	sm->fils_dh_group = dh_group;
	if (dh_group) {
		crypto_ecdh_deinit(sm->fils_ecdh);
		sm->fils_ecdh = crypto_ecdh_init(dh_group);
		if (!sm->fils_ecdh) {
			wpa_printf(MSG_INFO,
				   "FILS: Could not initialize ECDH with group %d",
				   dh_group);
			goto fail;
		}
		pub = crypto_ecdh_get_pubkey(sm->fils_ecdh, 1);
		if (!pub)
			goto fail;
		wpa_hexdump_buf(MSG_DEBUG, "FILS: Element (DH public key)",
				pub);
		sm->fils_dh_elem_len = wpabuf_len(pub);
	}
#endif /* CONFIG_FILS_SK_PFS */

	buf = wpabuf_alloc(1000 + sm->assoc_wpa_ie_len +
			   (pub ? wpabuf_len(pub) : 0));
	if (!buf)
		goto fail;

	/* Fields following the Authentication algorithm number field */

	/* Authentication Transaction seq# */
	wpabuf_put_le16(buf, 1);

	/* Status Code */
	wpabuf_put_le16(buf, WLAN_STATUS_SUCCESS);

	/* TODO: FILS PK */
#ifdef CONFIG_FILS_SK_PFS
	if (dh_group) {
		/* Finite Cyclic Group */
		wpabuf_put_le16(buf, dh_group);
		/* Element */
		wpabuf_put_buf(buf, pub);
	}
#endif /* CONFIG_FILS_SK_PFS */

	/* RSNE */
	wpa_hexdump(MSG_DEBUG, "FILS: RSNE in FILS Authentication frame",
		    sm->assoc_wpa_ie, sm->assoc_wpa_ie_len);
	wpabuf_put_data(buf, sm->assoc_wpa_ie, sm->assoc_wpa_ie_len);

	if (md) {
		/* MDE when using FILS for FT initial association */
		struct rsn_mdie *mdie;

		wpabuf_put_u8(buf, WLAN_EID_MOBILITY_DOMAIN);
		wpabuf_put_u8(buf, sizeof(*mdie));
		mdie = wpabuf_put(buf, sizeof(*mdie));
		os_memcpy(mdie->mobility_domain, md, MOBILITY_DOMAIN_ID_LEN);
		mdie->ft_capab = 0;
	}

	/* FILS Nonce */
	wpabuf_put_u8(buf, WLAN_EID_EXTENSION); /* Element ID */
	wpabuf_put_u8(buf, 1 + FILS_NONCE_LEN); /* Length */
	/* Element ID Extension */
	wpabuf_put_u8(buf, WLAN_EID_EXT_FILS_NONCE);
	wpabuf_put_data(buf, sm->fils_nonce, FILS_NONCE_LEN);

	/* FILS Session */
	wpabuf_put_u8(buf, WLAN_EID_EXTENSION); /* Element ID */
	wpabuf_put_u8(buf, 1 + FILS_SESSION_LEN); /* Length */
	/* Element ID Extension */
	wpabuf_put_u8(buf, WLAN_EID_EXT_FILS_SESSION);
	wpabuf_put_data(buf, sm->fils_session, FILS_SESSION_LEN);

	/* FILS Wrapped Data */
	sm->fils_erp_pmkid_set = 0;
	if (erp_msg) {
		wpabuf_put_u8(buf, WLAN_EID_EXTENSION); /* Element ID */
		wpabuf_put_u8(buf, 1 + wpabuf_len(erp_msg)); /* Length */
		/* Element ID Extension */
		wpabuf_put_u8(buf, WLAN_EID_EXT_FILS_WRAPPED_DATA);
		wpabuf_put_buf(buf, erp_msg);
		/* Calculate pending PMKID here so that we do not need to
		 * maintain a copy of the EAP-Initiate/Reauth message. */
		if (fils_pmkid_erp(sm->key_mgmt, wpabuf_head(erp_msg),
				   wpabuf_len(erp_msg),
				   sm->fils_erp_pmkid) == 0)
			sm->fils_erp_pmkid_set = 1;
	}

	wpa_hexdump_buf(MSG_DEBUG, "RSN: FILS fields for Authentication frame",
			buf);

fail:
	wpabuf_free(erp_msg);
	wpabuf_free(pub);
	return buf;
}


int fils_process_auth(struct wpa_sm *sm, const u8 *bssid, const u8 *data,
		      size_t len)
{
	const u8 *pos, *end;
	struct ieee802_11_elems elems;
	struct wpa_ie_data rsn;
	int pmkid_match = 0;
	u8 ick[FILS_ICK_MAX_LEN];
	size_t ick_len;
	int res;
	struct wpabuf *dh_ss = NULL;
	const u8 *g_sta = NULL;
	size_t g_sta_len = 0;
	const u8 *g_ap = NULL;
	size_t g_ap_len = 0;
	struct wpabuf *pub = NULL;

	os_memcpy(sm->bssid, bssid, ETH_ALEN);

	wpa_hexdump(MSG_DEBUG, "FILS: Authentication frame fields",
		    data, len);
	pos = data;
	end = data + len;

	/* TODO: FILS PK */
#ifdef CONFIG_FILS_SK_PFS
	if (sm->fils_dh_group) {
		u16 group;

		/* Using FILS PFS */

		/* Finite Cyclic Group */
		if (end - pos < 2) {
			wpa_printf(MSG_DEBUG,
				   "FILS: No room for Finite Cyclic Group");
			goto fail;
		}
		group = WPA_GET_LE16(pos);
		pos += 2;
		if (group != sm->fils_dh_group) {
			wpa_printf(MSG_DEBUG,
				   "FILS: Unexpected change in Finite Cyclic Group: %u (expected %u)",
				   group, sm->fils_dh_group);
			goto fail;
		}

		/* Element */
		if ((size_t) (end - pos) < sm->fils_dh_elem_len) {
			wpa_printf(MSG_DEBUG, "FILS: No room for Element");
			goto fail;
		}

		if (!sm->fils_ecdh) {
			wpa_printf(MSG_DEBUG, "FILS: No ECDH state available");
			goto fail;
		}
		dh_ss = crypto_ecdh_set_peerkey(sm->fils_ecdh, 1, pos,
						sm->fils_dh_elem_len);
		if (!dh_ss) {
			wpa_printf(MSG_DEBUG, "FILS: ECDH operation failed");
			goto fail;
		}
		wpa_hexdump_buf_key(MSG_DEBUG, "FILS: DH_SS", dh_ss);
		g_ap = pos;
		g_ap_len = sm->fils_dh_elem_len;
		pos += sm->fils_dh_elem_len;
	}
#endif /* CONFIG_FILS_SK_PFS */

	wpa_hexdump(MSG_DEBUG, "FILS: Remaining IEs", pos, end - pos);
	if (ieee802_11_parse_elems(pos, end - pos, &elems, 1) == ParseFailed) {
		wpa_printf(MSG_DEBUG, "FILS: Could not parse elements");
		goto fail;
	}

	/* RSNE */
	wpa_hexdump(MSG_DEBUG, "FILS: RSN element", elems.rsn_ie,
		    elems.rsn_ie_len);
	if (!elems.rsn_ie ||
	    wpa_parse_wpa_ie_rsn(elems.rsn_ie - 2, elems.rsn_ie_len + 2,
				 &rsn) < 0) {
		wpa_printf(MSG_DEBUG, "FILS: No RSN element");
		goto fail;
	}

	if (!elems.fils_nonce) {
		wpa_printf(MSG_DEBUG, "FILS: No FILS Nonce field");
		goto fail;
	}
	os_memcpy(sm->fils_anonce, elems.fils_nonce, FILS_NONCE_LEN);
	wpa_hexdump(MSG_DEBUG, "FILS: ANonce", sm->fils_anonce, FILS_NONCE_LEN);

#ifdef CONFIG_IEEE80211R
	if (wpa_key_mgmt_ft(sm->key_mgmt)) {
		struct wpa_ft_ies parse;

		if (!elems.mdie || !elems.ftie) {
			wpa_printf(MSG_DEBUG, "FILS+FT: No MDE or FTE");
			goto fail;
		}

		if (wpa_ft_parse_ies(pos, end - pos, &parse,
				     wpa_key_mgmt_sha384(sm->key_mgmt)) < 0) {
			wpa_printf(MSG_DEBUG, "FILS+FT: Failed to parse IEs");
			goto fail;
		}

		if (!parse.r0kh_id) {
			wpa_printf(MSG_DEBUG,
				   "FILS+FT: No R0KH-ID subelem in FTE");
			goto fail;
		}
		os_memcpy(sm->r0kh_id, parse.r0kh_id, parse.r0kh_id_len);
		sm->r0kh_id_len = parse.r0kh_id_len;
		wpa_hexdump_ascii(MSG_DEBUG, "FILS+FT: R0KH-ID",
				  sm->r0kh_id, sm->r0kh_id_len);

		if (!parse.r1kh_id) {
			wpa_printf(MSG_DEBUG,
				   "FILS+FT: No R1KH-ID subelem in FTE");
			goto fail;
		}
		os_memcpy(sm->r1kh_id, parse.r1kh_id, FT_R1KH_ID_LEN);
		wpa_hexdump(MSG_DEBUG, "FILS+FT: R1KH-ID",
			    sm->r1kh_id, FT_R1KH_ID_LEN);

		/* TODO: Check MDE and FTE payload */

		wpabuf_free(sm->fils_ft_ies);
		sm->fils_ft_ies = wpabuf_alloc(2 + elems.mdie_len +
					       2 + elems.ftie_len);
		if (!sm->fils_ft_ies)
			goto fail;
		wpabuf_put_data(sm->fils_ft_ies, elems.mdie - 2,
				2 + elems.mdie_len);
		wpabuf_put_data(sm->fils_ft_ies, elems.ftie - 2,
				2 + elems.ftie_len);
	} else {
		wpabuf_free(sm->fils_ft_ies);
		sm->fils_ft_ies = NULL;
	}
#endif /* CONFIG_IEEE80211R */

	/* PMKID List */
	if (rsn.pmkid && rsn.num_pmkid > 0) {
		wpa_hexdump(MSG_DEBUG, "FILS: PMKID List",
			    rsn.pmkid, rsn.num_pmkid * PMKID_LEN);

		if (rsn.num_pmkid != 1) {
			wpa_printf(MSG_DEBUG, "FILS: Invalid PMKID selection");
			goto fail;
		}
		wpa_hexdump(MSG_DEBUG, "FILS: PMKID", rsn.pmkid, PMKID_LEN);
		if (os_memcmp(sm->cur_pmksa->pmkid, rsn.pmkid, PMKID_LEN) != 0)
		{
			wpa_printf(MSG_DEBUG, "FILS: PMKID mismatch");
			wpa_hexdump(MSG_DEBUG, "FILS: Expected PMKID",
				    sm->cur_pmksa->pmkid, PMKID_LEN);
			goto fail;
		}
		wpa_printf(MSG_DEBUG,
			   "FILS: Matching PMKID - continue using PMKSA caching");
		pmkid_match = 1;
	}
	if (!pmkid_match && sm->cur_pmksa) {
		wpa_printf(MSG_DEBUG,
			   "FILS: No PMKID match - cannot use cached PMKSA entry");
		sm->cur_pmksa = NULL;
	}

	/* FILS Session */
	if (!elems.fils_session) {
		wpa_printf(MSG_DEBUG, "FILS: No FILS Session element");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "FILS: FILS Session", elems.fils_session,
		    FILS_SESSION_LEN);
	if (os_memcmp(sm->fils_session, elems.fils_session, FILS_SESSION_LEN)
	    != 0) {
		wpa_printf(MSG_DEBUG, "FILS: Session mismatch");
		wpa_hexdump(MSG_DEBUG, "FILS: Expected FILS Session",
			    sm->fils_session, FILS_SESSION_LEN);
		goto fail;
	}

	/* FILS Wrapped Data */
	if (!sm->cur_pmksa && elems.fils_wrapped_data) {
		u8 rmsk[ERP_MAX_KEY_LEN];
		size_t rmsk_len;

		wpa_hexdump(MSG_DEBUG, "FILS: Wrapped Data",
			    elems.fils_wrapped_data,
			    elems.fils_wrapped_data_len);
		eapol_sm_process_erp_finish(sm->eapol, elems.fils_wrapped_data,
					    elems.fils_wrapped_data_len);
		if (eapol_sm_failed(sm->eapol))
			goto fail;

		rmsk_len = ERP_MAX_KEY_LEN;
		res = eapol_sm_get_key(sm->eapol, rmsk, rmsk_len);
		if (res == PMK_LEN) {
			rmsk_len = PMK_LEN;
			res = eapol_sm_get_key(sm->eapol, rmsk, rmsk_len);
		}
		if (res)
			goto fail;

		res = fils_rmsk_to_pmk(sm->key_mgmt, rmsk, rmsk_len,
				       sm->fils_nonce, sm->fils_anonce,
				       dh_ss ? wpabuf_head(dh_ss) : NULL,
				       dh_ss ? wpabuf_len(dh_ss) : 0,
				       sm->pmk, &sm->pmk_len);
		os_memset(rmsk, 0, sizeof(rmsk));

		/* Don't use DHss in PTK derivation if PMKSA caching is not
		 * used. */
		wpabuf_clear_free(dh_ss);
		dh_ss = NULL;

		if (res)
			goto fail;

		if (!sm->fils_erp_pmkid_set) {
			wpa_printf(MSG_DEBUG, "FILS: PMKID not available");
			goto fail;
		}
		wpa_hexdump(MSG_DEBUG, "FILS: PMKID", sm->fils_erp_pmkid,
			    PMKID_LEN);
		wpa_printf(MSG_DEBUG, "FILS: ERP processing succeeded - add PMKSA cache entry for the result");
		sm->cur_pmksa = pmksa_cache_add(sm->pmksa, sm->pmk, sm->pmk_len,
						sm->fils_erp_pmkid, NULL, 0,
						sm->bssid, sm->own_addr,
						sm->network_ctx, sm->key_mgmt,
						NULL);
	}

	if (!sm->cur_pmksa) {
		wpa_printf(MSG_DEBUG,
			   "FILS: No remaining options to continue FILS authentication");
		goto fail;
	}

	if (fils_pmk_to_ptk(sm->pmk, sm->pmk_len, sm->own_addr, sm->bssid,
			    sm->fils_nonce, sm->fils_anonce,
			    dh_ss ? wpabuf_head(dh_ss) : NULL,
			    dh_ss ? wpabuf_len(dh_ss) : 0,
			    &sm->ptk, ick, &ick_len,
			    sm->key_mgmt, sm->pairwise_cipher,
			    sm->fils_ft, &sm->fils_ft_len) < 0) {
		wpa_printf(MSG_DEBUG, "FILS: Failed to derive PTK");
		goto fail;
	}

	wpabuf_clear_free(dh_ss);
	dh_ss = NULL;

	sm->ptk_set = 1;
	sm->tptk_set = 0;
	os_memset(&sm->tptk, 0, sizeof(sm->tptk));

#ifdef CONFIG_FILS_SK_PFS
	if (sm->fils_dh_group) {
		if (!sm->fils_ecdh) {
			wpa_printf(MSG_INFO, "FILS: ECDH not initialized");
			goto fail;
		}
		pub = crypto_ecdh_get_pubkey(sm->fils_ecdh, 1);
		if (!pub)
			goto fail;
		wpa_hexdump_buf(MSG_DEBUG, "FILS: gSTA", pub);
		g_sta = wpabuf_head(pub);
		g_sta_len = wpabuf_len(pub);
		if (!g_ap) {
			wpa_printf(MSG_INFO, "FILS: gAP not available");
			goto fail;
		}
		wpa_hexdump(MSG_DEBUG, "FILS: gAP", g_ap, g_ap_len);
	}
#endif /* CONFIG_FILS_SK_PFS */

	res = fils_key_auth_sk(ick, ick_len, sm->fils_nonce,
			       sm->fils_anonce, sm->own_addr, sm->bssid,
			       g_sta, g_sta_len, g_ap, g_ap_len,
			       sm->key_mgmt, sm->fils_key_auth_sta,
			       sm->fils_key_auth_ap,
			       &sm->fils_key_auth_len);
	wpabuf_free(pub);
	os_memset(ick, 0, sizeof(ick));
	return res;
fail:
	wpabuf_free(pub);
	wpabuf_clear_free(dh_ss);
	return -1;
}


#ifdef CONFIG_IEEE80211R
static int fils_ft_build_assoc_req_rsne(struct wpa_sm *sm, struct wpabuf *buf)
{
	struct rsn_ie_hdr *rsnie;
	u16 capab;
	u8 *pos;
	int use_sha384 = wpa_key_mgmt_sha384(sm->key_mgmt);

	/* RSNIE[PMKR0Name/PMKR1Name] */
	rsnie = wpabuf_put(buf, sizeof(*rsnie));
	rsnie->elem_id = WLAN_EID_RSN;
	WPA_PUT_LE16(rsnie->version, RSN_VERSION);

	/* Group Suite Selector */
	if (!wpa_cipher_valid_group(sm->group_cipher)) {
		wpa_printf(MSG_WARNING, "FT: Invalid group cipher (%d)",
			   sm->group_cipher);
		return -1;
	}
	pos = wpabuf_put(buf, RSN_SELECTOR_LEN);
	RSN_SELECTOR_PUT(pos, wpa_cipher_to_suite(WPA_PROTO_RSN,
						  sm->group_cipher));

	/* Pairwise Suite Count */
	wpabuf_put_le16(buf, 1);

	/* Pairwise Suite List */
	if (!wpa_cipher_valid_pairwise(sm->pairwise_cipher)) {
		wpa_printf(MSG_WARNING, "FT: Invalid pairwise cipher (%d)",
			   sm->pairwise_cipher);
		return -1;
	}
	pos = wpabuf_put(buf, RSN_SELECTOR_LEN);
	RSN_SELECTOR_PUT(pos, wpa_cipher_to_suite(WPA_PROTO_RSN,
						  sm->pairwise_cipher));

	/* Authenticated Key Management Suite Count */
	wpabuf_put_le16(buf, 1);

	/* Authenticated Key Management Suite List */
	pos = wpabuf_put(buf, RSN_SELECTOR_LEN);
	if (sm->key_mgmt == WPA_KEY_MGMT_FT_FILS_SHA256)
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_FT_FILS_SHA256);
	else if (sm->key_mgmt == WPA_KEY_MGMT_FT_FILS_SHA384)
		RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_FT_FILS_SHA384);
	else {
		wpa_printf(MSG_WARNING,
			   "FILS+FT: Invalid key management type (%d)",
			   sm->key_mgmt);
		return -1;
	}

	/* RSN Capabilities */
	capab = 0;
#ifdef CONFIG_IEEE80211W
	if (sm->mgmt_group_cipher == WPA_CIPHER_AES_128_CMAC)
		capab |= WPA_CAPABILITY_MFPC;
#endif /* CONFIG_IEEE80211W */
	wpabuf_put_le16(buf, capab);

	/* PMKID Count */
	wpabuf_put_le16(buf, 1);

	/* PMKID List [PMKR1Name] */
	wpa_hexdump_key(MSG_DEBUG, "FILS+FT: XXKey (FILS-FT)",
			sm->fils_ft, sm->fils_ft_len);
	wpa_hexdump_ascii(MSG_DEBUG, "FILS+FT: SSID", sm->ssid, sm->ssid_len);
	wpa_hexdump(MSG_DEBUG, "FILS+FT: MDID",
		    sm->mobility_domain, MOBILITY_DOMAIN_ID_LEN);
	wpa_hexdump_ascii(MSG_DEBUG, "FILS+FT: R0KH-ID",
			  sm->r0kh_id, sm->r0kh_id_len);
	if (wpa_derive_pmk_r0(sm->fils_ft, sm->fils_ft_len, sm->ssid,
			      sm->ssid_len, sm->mobility_domain,
			      sm->r0kh_id, sm->r0kh_id_len, sm->own_addr,
			      sm->pmk_r0, sm->pmk_r0_name, use_sha384) < 0) {
		wpa_printf(MSG_WARNING, "FILS+FT: Could not derive PMK-R0");
		return -1;
	}
	sm->pmk_r0_len = use_sha384 ? SHA384_MAC_LEN : PMK_LEN;
	wpa_hexdump_key(MSG_DEBUG, "FILS+FT: PMK-R0",
			sm->pmk_r0, sm->pmk_r0_len);
	wpa_hexdump(MSG_DEBUG, "FILS+FT: PMKR0Name",
		    sm->pmk_r0_name, WPA_PMK_NAME_LEN);
	wpa_printf(MSG_DEBUG, "FILS+FT: R1KH-ID: " MACSTR,
		   MAC2STR(sm->r1kh_id));
	pos = wpabuf_put(buf, WPA_PMK_NAME_LEN);
	if (wpa_derive_pmk_r1_name(sm->pmk_r0_name, sm->r1kh_id, sm->own_addr,
				   pos, use_sha384) < 0) {
		wpa_printf(MSG_WARNING, "FILS+FT: Could not derive PMKR1Name");
		return -1;
	}
	wpa_hexdump(MSG_DEBUG, "FILS+FT: PMKR1Name", pos, WPA_PMK_NAME_LEN);

#ifdef CONFIG_IEEE80211W
	if (sm->mgmt_group_cipher == WPA_CIPHER_AES_128_CMAC) {
		/* Management Group Cipher Suite */
		pos = wpabuf_put(buf, RSN_SELECTOR_LEN);
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_AES_128_CMAC);
	}
#endif /* CONFIG_IEEE80211W */

	rsnie->len = ((u8 *) wpabuf_put(buf, 0) - (u8 *) rsnie) - 2;
	return 0;
}
#endif /* CONFIG_IEEE80211R */


struct wpabuf * fils_build_assoc_req(struct wpa_sm *sm, const u8 **kek,
				     size_t *kek_len, const u8 **snonce,
				     const u8 **anonce,
				     const struct wpabuf **hlp,
				     unsigned int num_hlp)
{
	struct wpabuf *buf;
	size_t len;
	unsigned int i;

	len = 1000;
#ifdef CONFIG_IEEE80211R
	if (sm->fils_ft_ies)
		len += wpabuf_len(sm->fils_ft_ies);
	if (wpa_key_mgmt_ft(sm->key_mgmt))
		len += 256;
#endif /* CONFIG_IEEE80211R */
	for (i = 0; hlp && i < num_hlp; i++)
		len += 10 + wpabuf_len(hlp[i]);
	buf = wpabuf_alloc(len);
	if (!buf)
		return NULL;

#ifdef CONFIG_IEEE80211R
	if (wpa_key_mgmt_ft(sm->key_mgmt) && sm->fils_ft_ies) {
		/* MDE and FTE when using FILS+FT */
		wpabuf_put_buf(buf, sm->fils_ft_ies);
		/* RSNE with PMKR1Name in PMKID field */
		if (fils_ft_build_assoc_req_rsne(sm, buf) < 0) {
			wpabuf_free(buf);
			return NULL;
		}
	}
#endif /* CONFIG_IEEE80211R */

	/* FILS Session */
	wpabuf_put_u8(buf, WLAN_EID_EXTENSION); /* Element ID */
	wpabuf_put_u8(buf, 1 + FILS_SESSION_LEN); /* Length */
	/* Element ID Extension */
	wpabuf_put_u8(buf, WLAN_EID_EXT_FILS_SESSION);
	wpabuf_put_data(buf, sm->fils_session, FILS_SESSION_LEN);

	/* Everything after FILS Session element gets encrypted in the driver
	 * with KEK. The buffer returned from here is the plaintext version. */

	/* TODO: FILS Public Key */

	/* FILS Key Confirm */
	wpabuf_put_u8(buf, WLAN_EID_EXTENSION); /* Element ID */
	wpabuf_put_u8(buf, 1 + sm->fils_key_auth_len); /* Length */
	/* Element ID Extension */
	wpabuf_put_u8(buf, WLAN_EID_EXT_FILS_KEY_CONFIRM);
	wpabuf_put_data(buf, sm->fils_key_auth_sta, sm->fils_key_auth_len);

	/* FILS HLP Container */
	for (i = 0; hlp && i < num_hlp; i++) {
		const u8 *pos = wpabuf_head(hlp[i]);
		size_t left = wpabuf_len(hlp[i]);

		wpabuf_put_u8(buf, WLAN_EID_EXTENSION); /* Element ID */
		if (left <= 254)
			len = 1 + left;
		else
			len = 255;
		wpabuf_put_u8(buf, len); /* Length */
		/* Element ID Extension */
		wpabuf_put_u8(buf, WLAN_EID_EXT_FILS_HLP_CONTAINER);
		/* Destination MAC Address, Source MAC Address, HLP Packet.
		 * HLP Packet is in MSDU format (i.e., included the LLC/SNAP
		 * header when LPD is used). */
		wpabuf_put_data(buf, pos, len - 1);
		pos += len - 1;
		left -= len - 1;
		while (left) {
			wpabuf_put_u8(buf, WLAN_EID_FRAGMENT);
			len = left > 255 ? 255 : left;
			wpabuf_put_u8(buf, len);
			wpabuf_put_data(buf, pos, len);
			pos += len;
			left -= len;
		}
	}

	/* TODO: FILS IP Address Assignment */

	wpa_hexdump_buf(MSG_DEBUG, "FILS: Association Request plaintext", buf);

	*kek = sm->ptk.kek;
	*kek_len = sm->ptk.kek_len;
	wpa_hexdump_key(MSG_DEBUG, "FILS: KEK for AEAD", *kek, *kek_len);
	*snonce = sm->fils_nonce;
	wpa_hexdump(MSG_DEBUG, "FILS: SNonce for AEAD AAD",
		    *snonce, FILS_NONCE_LEN);
	*anonce = sm->fils_anonce;
	wpa_hexdump(MSG_DEBUG, "FILS: ANonce for AEAD AAD",
		    *anonce, FILS_NONCE_LEN);

	return buf;
}


static void fils_process_hlp_resp(struct wpa_sm *sm, const u8 *resp, size_t len)
{
	const u8 *pos, *end;

	wpa_hexdump(MSG_MSGDUMP, "FILS: HLP response", resp, len);
	if (len < 2 * ETH_ALEN)
		return;
	pos = resp + 2 * ETH_ALEN;
	end = resp + len;
	if (end - pos >= 6 &&
	    os_memcmp(pos, "\xaa\xaa\x03\x00\x00\x00", 6) == 0)
		pos += 6; /* Remove SNAP/LLC header */
	wpa_sm_fils_hlp_rx(sm, resp, resp + ETH_ALEN, pos, end - pos);
}


static void fils_process_hlp_container(struct wpa_sm *sm, const u8 *pos,
				       size_t len)
{
	const u8 *end = pos + len;
	u8 *tmp, *tmp_pos;

	/* Check if there are any FILS HLP Container elements */
	while (end - pos >= 2) {
		if (2 + pos[1] > end - pos)
			return;
		if (pos[0] == WLAN_EID_EXTENSION &&
		    pos[1] >= 1 + 2 * ETH_ALEN &&
		    pos[2] == WLAN_EID_EXT_FILS_HLP_CONTAINER)
			break;
		pos += 2 + pos[1];
	}
	if (end - pos < 2)
		return; /* No FILS HLP Container elements */

	tmp = os_malloc(end - pos);
	if (!tmp)
		return;

	while (end - pos >= 2) {
		if (2 + pos[1] > end - pos ||
		    pos[0] != WLAN_EID_EXTENSION ||
		    pos[1] < 1 + 2 * ETH_ALEN ||
		    pos[2] != WLAN_EID_EXT_FILS_HLP_CONTAINER)
			break;
		tmp_pos = tmp;
		os_memcpy(tmp_pos, pos + 3, pos[1] - 1);
		tmp_pos += pos[1] - 1;
		pos += 2 + pos[1];

		/* Add possible fragments */
		while (end - pos >= 2 && pos[0] == WLAN_EID_FRAGMENT &&
		       2 + pos[1] <= end - pos) {
			os_memcpy(tmp_pos, pos + 2, pos[1]);
			tmp_pos += pos[1];
			pos += 2 + pos[1];
		}

		fils_process_hlp_resp(sm, tmp, tmp_pos - tmp);
	}

	os_free(tmp);
}


int fils_process_assoc_resp(struct wpa_sm *sm, const u8 *resp, size_t len)
{
	const struct ieee80211_mgmt *mgmt;
	const u8 *end, *ie_start;
	struct ieee802_11_elems elems;
	int keylen, rsclen;
	enum wpa_alg alg;
	struct wpa_gtk_data gd;
	int maxkeylen;
	struct wpa_eapol_ie_parse kde;

	if (!sm || !sm->ptk_set) {
		wpa_printf(MSG_DEBUG, "FILS: No KEK available");
		return -1;
	}

	if (!wpa_key_mgmt_fils(sm->key_mgmt)) {
		wpa_printf(MSG_DEBUG, "FILS: Not a FILS AKM");
		return -1;
	}

	if (sm->fils_completed) {
		wpa_printf(MSG_DEBUG,
			   "FILS: Association has already been completed for this FILS authentication - ignore unexpected retransmission");
		return -1;
	}

	wpa_hexdump(MSG_DEBUG, "FILS: (Re)Association Response frame",
		    resp, len);

	mgmt = (const struct ieee80211_mgmt *) resp;
	if (len < IEEE80211_HDRLEN + sizeof(mgmt->u.assoc_resp))
		return -1;

	end = resp + len;
	/* Same offset for Association Response and Reassociation Response */
	ie_start = mgmt->u.assoc_resp.variable;

	if (ieee802_11_parse_elems(ie_start, end - ie_start, &elems, 1) ==
	    ParseFailed) {
		wpa_printf(MSG_DEBUG,
			   "FILS: Failed to parse decrypted elements");
		goto fail;
	}

	if (!elems.fils_session) {
		wpa_printf(MSG_DEBUG, "FILS: No FILS Session element");
		return -1;
	}
	if (os_memcmp(elems.fils_session, sm->fils_session,
		      FILS_SESSION_LEN) != 0) {
		wpa_printf(MSG_DEBUG, "FILS: FILS Session mismatch");
		wpa_hexdump(MSG_DEBUG, "FILS: Received FILS Session",
			    elems.fils_session, FILS_SESSION_LEN);
		wpa_hexdump(MSG_DEBUG, "FILS: Expected FILS Session",
			    sm->fils_session, FILS_SESSION_LEN);
	}

	/* TODO: FILS Public Key */

	if (!elems.fils_key_confirm) {
		wpa_printf(MSG_DEBUG, "FILS: No FILS Key Confirm element");
		goto fail;
	}
	if (elems.fils_key_confirm_len != sm->fils_key_auth_len) {
		wpa_printf(MSG_DEBUG,
			   "FILS: Unexpected Key-Auth length %d (expected %d)",
			   elems.fils_key_confirm_len,
			   (int) sm->fils_key_auth_len);
		goto fail;
	}
	if (os_memcmp(elems.fils_key_confirm, sm->fils_key_auth_ap,
		      sm->fils_key_auth_len) != 0) {
		wpa_printf(MSG_DEBUG, "FILS: Key-Auth mismatch");
		wpa_hexdump(MSG_DEBUG, "FILS: Received Key-Auth",
			    elems.fils_key_confirm,
			    elems.fils_key_confirm_len);
		wpa_hexdump(MSG_DEBUG, "FILS: Expected Key-Auth",
			    sm->fils_key_auth_ap, sm->fils_key_auth_len);
		goto fail;
	}

	/* Key Delivery */
	if (!elems.key_delivery) {
		wpa_printf(MSG_DEBUG, "FILS: No Key Delivery element");
		goto fail;
	}

	/* Parse GTK and set the key to the driver */
	os_memset(&gd, 0, sizeof(gd));
	if (wpa_supplicant_parse_ies(elems.key_delivery + WPA_KEY_RSC_LEN,
				     elems.key_delivery_len - WPA_KEY_RSC_LEN,
				     &kde) < 0) {
		wpa_printf(MSG_DEBUG, "FILS: Failed to parse KDEs");
		goto fail;
	}
	if (!kde.gtk) {
		wpa_printf(MSG_DEBUG, "FILS: No GTK KDE");
		goto fail;
	}
	maxkeylen = gd.gtk_len = kde.gtk_len - 2;
	if (wpa_supplicant_check_group_cipher(sm, sm->group_cipher,
					      gd.gtk_len, maxkeylen,
					      &gd.key_rsc_len, &gd.alg))
		goto fail;

	wpa_hexdump_key(MSG_DEBUG, "FILS: Received GTK", kde.gtk, kde.gtk_len);
	gd.keyidx = kde.gtk[0] & 0x3;
	gd.tx = wpa_supplicant_gtk_tx_bit_workaround(sm,
						     !!(kde.gtk[0] & BIT(2)));
	if (kde.gtk_len - 2 > sizeof(gd.gtk)) {
		wpa_printf(MSG_DEBUG, "FILS: Too long GTK in GTK KDE (len=%lu)",
			   (unsigned long) kde.gtk_len - 2);
		goto fail;
	}
	os_memcpy(gd.gtk, kde.gtk + 2, kde.gtk_len - 2);

	wpa_printf(MSG_DEBUG, "FILS: Set GTK to driver");
	if (wpa_supplicant_install_gtk(sm, &gd, elems.key_delivery, 0) < 0) {
		wpa_printf(MSG_DEBUG, "FILS: Failed to set GTK");
		goto fail;
	}

	if (ieee80211w_set_keys(sm, &kde) < 0) {
		wpa_printf(MSG_DEBUG, "FILS: Failed to set IGTK");
		goto fail;
	}

	alg = wpa_cipher_to_alg(sm->pairwise_cipher);
	keylen = wpa_cipher_key_len(sm->pairwise_cipher);
	if (keylen <= 0 || (unsigned int) keylen != sm->ptk.tk_len) {
		wpa_printf(MSG_DEBUG, "FILS: TK length mismatch: %u != %lu",
			   keylen, (long unsigned int) sm->ptk.tk_len);
		goto fail;
	}
	rsclen = wpa_cipher_rsc_len(sm->pairwise_cipher);
	wpa_hexdump_key(MSG_DEBUG, "FILS: Set TK to driver",
			sm->ptk.tk, keylen);
	if (wpa_sm_set_key(sm, alg, sm->bssid, 0, 1, null_rsc, rsclen,
			   sm->ptk.tk, keylen) < 0) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"FILS: Failed to set PTK to the driver (alg=%d keylen=%d bssid="
			MACSTR ")",
			alg, keylen, MAC2STR(sm->bssid));
		goto fail;
	}

	/* TODO: TK could be cleared after auth frame exchange now that driver
	 * takes care of association frame encryption/decryption. */
	/* TK is not needed anymore in supplicant */
	os_memset(sm->ptk.tk, 0, WPA_TK_MAX_LEN);
	sm->ptk.tk_len = 0;
	sm->ptk.installed = 1;

	/* FILS HLP Container */
	fils_process_hlp_container(sm, ie_start, end - ie_start);

	/* TODO: FILS IP Address Assignment */

	wpa_printf(MSG_DEBUG, "FILS: Auth+Assoc completed successfully");
	sm->fils_completed = 1;

	return 0;
fail:
	return -1;
}


void wpa_sm_set_reset_fils_completed(struct wpa_sm *sm, int set)
{
	if (sm)
		sm->fils_completed = !!set;
}

#endif /* CONFIG_FILS */


int wpa_fils_is_completed(struct wpa_sm *sm)
{
#ifdef CONFIG_FILS
	return sm && sm->fils_completed;
#else /* CONFIG_FILS */
	return 0;
#endif /* CONFIG_FILS */
}


#ifdef CONFIG_OWE

struct wpabuf * owe_build_assoc_req(struct wpa_sm *sm, u16 group)
{
	struct wpabuf *ie = NULL, *pub = NULL;
	size_t prime_len;

	if (group == 19)
		prime_len = 32;
	else if (group == 20)
		prime_len = 48;
	else if (group == 21)
		prime_len = 66;
	else
		return NULL;

	crypto_ecdh_deinit(sm->owe_ecdh);
	sm->owe_ecdh = crypto_ecdh_init(group);
	if (!sm->owe_ecdh)
		goto fail;
	sm->owe_group = group;
	pub = crypto_ecdh_get_pubkey(sm->owe_ecdh, 0);
	pub = wpabuf_zeropad(pub, prime_len);
	if (!pub)
		goto fail;

	ie = wpabuf_alloc(5 + wpabuf_len(pub));
	if (!ie)
		goto fail;
	wpabuf_put_u8(ie, WLAN_EID_EXTENSION);
	wpabuf_put_u8(ie, 1 + 2 + wpabuf_len(pub));
	wpabuf_put_u8(ie, WLAN_EID_EXT_OWE_DH_PARAM);
	wpabuf_put_le16(ie, group);
	wpabuf_put_buf(ie, pub);
	wpabuf_free(pub);
	wpa_hexdump_buf(MSG_DEBUG, "OWE: Diffie-Hellman Parameter element",
			ie);

	return ie;
fail:
	wpabuf_free(pub);
	crypto_ecdh_deinit(sm->owe_ecdh);
	sm->owe_ecdh = NULL;
	return NULL;
}


int owe_process_assoc_resp(struct wpa_sm *sm, const u8 *bssid,
			   const u8 *resp_ies, size_t resp_ies_len)
{
	struct ieee802_11_elems elems;
	u16 group;
	struct wpabuf *secret, *pub, *hkey;
	int res;
	u8 prk[SHA512_MAC_LEN], pmkid[SHA512_MAC_LEN];
	const char *info = "OWE Key Generation";
	const u8 *addr[2];
	size_t len[2];
	size_t hash_len, prime_len;
	struct wpa_ie_data data;

	if (!resp_ies ||
	    ieee802_11_parse_elems(resp_ies, resp_ies_len, &elems, 1) ==
	    ParseFailed) {
		wpa_printf(MSG_INFO,
			   "OWE: Could not parse Association Response frame elements");
		return -1;
	}

	if (sm->cur_pmksa && elems.rsn_ie &&
	    wpa_parse_wpa_ie_rsn(elems.rsn_ie - 2, 2 + elems.rsn_ie_len,
				 &data) == 0 &&
	    data.num_pmkid == 1 && data.pmkid &&
	    os_memcmp(sm->cur_pmksa->pmkid, data.pmkid, PMKID_LEN) == 0) {
		wpa_printf(MSG_DEBUG, "OWE: Use PMKSA caching");
		wpa_sm_set_pmk_from_pmksa(sm);
		return 0;
	}

	if (!elems.owe_dh) {
		wpa_printf(MSG_INFO,
			   "OWE: No Diffie-Hellman Parameter element found in Association Response frame");
		return -1;
	}

	group = WPA_GET_LE16(elems.owe_dh);
	if (group != sm->owe_group) {
		wpa_printf(MSG_INFO,
			   "OWE: Unexpected Diffie-Hellman group in response: %u",
			   group);
		return -1;
	}

	if (!sm->owe_ecdh) {
		wpa_printf(MSG_INFO, "OWE: No ECDH state available");
		return -1;
	}

	if (group == 19)
		prime_len = 32;
	else if (group == 20)
		prime_len = 48;
	else if (group == 21)
		prime_len = 66;
	else
		return -1;

	secret = crypto_ecdh_set_peerkey(sm->owe_ecdh, 0,
					 elems.owe_dh + 2,
					 elems.owe_dh_len - 2);
	secret = wpabuf_zeropad(secret, prime_len);
	if (!secret) {
		wpa_printf(MSG_DEBUG, "OWE: Invalid peer DH public key");
		return -1;
	}
	wpa_hexdump_buf_key(MSG_DEBUG, "OWE: DH shared secret", secret);

	/* prk = HKDF-extract(C | A | group, z) */

	pub = crypto_ecdh_get_pubkey(sm->owe_ecdh, 0);
	if (!pub) {
		wpabuf_clear_free(secret);
		return -1;
	}

	/* PMKID = Truncate-128(Hash(C | A)) */
	addr[0] = wpabuf_head(pub);
	len[0] = wpabuf_len(pub);
	addr[1] = elems.owe_dh + 2;
	len[1] = elems.owe_dh_len - 2;
	if (group == 19) {
		res = sha256_vector(2, addr, len, pmkid);
		hash_len = SHA256_MAC_LEN;
	} else if (group == 20) {
		res = sha384_vector(2, addr, len, pmkid);
		hash_len = SHA384_MAC_LEN;
	} else if (group == 21) {
		res = sha512_vector(2, addr, len, pmkid);
		hash_len = SHA512_MAC_LEN;
	} else {
		res = -1;
		hash_len = 0;
	}
	pub = wpabuf_zeropad(pub, prime_len);
	if (res < 0 || !pub) {
		wpabuf_free(pub);
		wpabuf_clear_free(secret);
		return -1;
	}

	hkey = wpabuf_alloc(wpabuf_len(pub) + elems.owe_dh_len - 2 + 2);
	if (!hkey) {
		wpabuf_free(pub);
		wpabuf_clear_free(secret);
		return -1;
	}

	wpabuf_put_buf(hkey, pub); /* C */
	wpabuf_free(pub);
	wpabuf_put_data(hkey, elems.owe_dh + 2, elems.owe_dh_len - 2); /* A */
	wpabuf_put_le16(hkey, sm->owe_group); /* group */
	if (group == 19)
		res = hmac_sha256(wpabuf_head(hkey), wpabuf_len(hkey),
				  wpabuf_head(secret), wpabuf_len(secret), prk);
	else if (group == 20)
		res = hmac_sha384(wpabuf_head(hkey), wpabuf_len(hkey),
				  wpabuf_head(secret), wpabuf_len(secret), prk);
	else if (group == 21)
		res = hmac_sha512(wpabuf_head(hkey), wpabuf_len(hkey),
				  wpabuf_head(secret), wpabuf_len(secret), prk);
	wpabuf_clear_free(hkey);
	wpabuf_clear_free(secret);
	if (res < 0)
		return -1;

	wpa_hexdump_key(MSG_DEBUG, "OWE: prk", prk, hash_len);

	/* PMK = HKDF-expand(prk, "OWE Key Generation", n) */

	if (group == 19)
		res = hmac_sha256_kdf(prk, hash_len, NULL, (const u8 *) info,
				      os_strlen(info), sm->pmk, hash_len);
	else if (group == 20)
		res = hmac_sha384_kdf(prk, hash_len, NULL, (const u8 *) info,
				      os_strlen(info), sm->pmk, hash_len);
	else if (group == 21)
		res = hmac_sha512_kdf(prk, hash_len, NULL, (const u8 *) info,
				      os_strlen(info), sm->pmk, hash_len);
	os_memset(prk, 0, SHA512_MAC_LEN);
	if (res < 0) {
		sm->pmk_len = 0;
		return -1;
	}
	sm->pmk_len = hash_len;

	wpa_hexdump_key(MSG_DEBUG, "OWE: PMK", sm->pmk, sm->pmk_len);
	wpa_hexdump(MSG_DEBUG, "OWE: PMKID", pmkid, PMKID_LEN);
	pmksa_cache_add(sm->pmksa, sm->pmk, sm->pmk_len, pmkid, NULL, 0,
			bssid, sm->own_addr, sm->network_ctx, sm->key_mgmt,
			NULL);

	return 0;
}

#endif /* CONFIG_OWE */


void wpa_sm_set_fils_cache_id(struct wpa_sm *sm, const u8 *fils_cache_id)
{
#ifdef CONFIG_FILS
	if (sm && fils_cache_id) {
		sm->fils_cache_id_set = 1;
		os_memcpy(sm->fils_cache_id, fils_cache_id, FILS_CACHE_ID_LEN);
	}
#endif /* CONFIG_FILS */
}
