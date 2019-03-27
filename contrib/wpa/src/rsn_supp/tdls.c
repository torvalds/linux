/*
 * wpa_supplicant - TDLS
 * Copyright (c) 2010-2011, Atheros Communications
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "utils/os.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "crypto/sha256.h"
#include "crypto/crypto.h"
#include "crypto/aes_wrap.h"
#include "rsn_supp/wpa.h"
#include "rsn_supp/wpa_ie.h"
#include "rsn_supp/wpa_i.h"
#include "drivers/driver.h"
#include "l2_packet/l2_packet.h"

#ifdef CONFIG_TDLS_TESTING
#define TDLS_TESTING_LONG_FRAME BIT(0)
#define TDLS_TESTING_ALT_RSN_IE BIT(1)
#define TDLS_TESTING_DIFF_BSSID BIT(2)
#define TDLS_TESTING_SHORT_LIFETIME BIT(3)
#define TDLS_TESTING_WRONG_LIFETIME_RESP BIT(4)
#define TDLS_TESTING_WRONG_LIFETIME_CONF BIT(5)
#define TDLS_TESTING_LONG_LIFETIME BIT(6)
#define TDLS_TESTING_CONCURRENT_INIT BIT(7)
#define TDLS_TESTING_NO_TPK_EXPIRATION BIT(8)
#define TDLS_TESTING_DECLINE_RESP BIT(9)
#define TDLS_TESTING_IGNORE_AP_PROHIBIT BIT(10)
#define TDLS_TESTING_WRONG_MIC BIT(11)
#define TDLS_TESTING_DOUBLE_TPK_M2 BIT(12)
unsigned int tdls_testing = 0;
#endif /* CONFIG_TDLS_TESTING */

#define TPK_LIFETIME 43200 /* 12 hours */
#define TPK_M1_RETRY_COUNT 3
#define TPK_M1_TIMEOUT 5000 /* in milliseconds */
#define TPK_M2_RETRY_COUNT 10
#define TPK_M2_TIMEOUT 500 /* in milliseconds */

#define TDLS_MIC_LEN		16

#define TDLS_TIMEOUT_LEN	4

struct wpa_tdls_ftie {
	u8 ie_type; /* FTIE */
	u8 ie_len;
	u8 mic_ctrl[2];
	u8 mic[TDLS_MIC_LEN];
	u8 Anonce[WPA_NONCE_LEN]; /* Responder Nonce in TDLS */
	u8 Snonce[WPA_NONCE_LEN]; /* Initiator Nonce in TDLS */
	/* followed by optional elements */
} STRUCT_PACKED;

struct wpa_tdls_timeoutie {
	u8 ie_type; /* Timeout IE */
	u8 ie_len;
	u8 interval_type;
	u8 value[TDLS_TIMEOUT_LEN];
} STRUCT_PACKED;

struct wpa_tdls_lnkid {
	u8 ie_type; /* Link Identifier IE */
	u8 ie_len;
	u8 bssid[ETH_ALEN];
	u8 init_sta[ETH_ALEN];
	u8 resp_sta[ETH_ALEN];
} STRUCT_PACKED;

/* TDLS frame headers as per IEEE Std 802.11z-2010 */
struct wpa_tdls_frame {
	u8 payloadtype; /* IEEE80211_TDLS_RFTYPE */
	u8 category; /* Category */
	u8 action; /* Action (enum tdls_frame_type) */
} STRUCT_PACKED;

static u8 * wpa_add_tdls_timeoutie(u8 *pos, u8 *ie, size_t ie_len, u32 tsecs);
static void wpa_tdls_tpk_retry_timeout(void *eloop_ctx, void *timeout_ctx);
static void wpa_tdls_peer_free(struct wpa_sm *sm, struct wpa_tdls_peer *peer);
static void wpa_tdls_disable_peer_link(struct wpa_sm *sm,
				       struct wpa_tdls_peer *peer);
static int wpa_tdls_send_teardown(struct wpa_sm *sm, const u8 *addr,
				  u16 reason_code);


#define TDLS_MAX_IE_LEN 80
#define IEEE80211_MAX_SUPP_RATES 32

struct wpa_tdls_peer {
	struct wpa_tdls_peer *next;
	unsigned int reconfig_key:1;
	int initiator; /* whether this end was initiator for TDLS setup */
	u8 addr[ETH_ALEN]; /* other end MAC address */
	u8 inonce[WPA_NONCE_LEN]; /* Initiator Nonce */
	u8 rnonce[WPA_NONCE_LEN]; /* Responder Nonce */
	u8 rsnie_i[TDLS_MAX_IE_LEN]; /* Initiator RSN IE */
	size_t rsnie_i_len;
	u8 rsnie_p[TDLS_MAX_IE_LEN]; /* Peer RSN IE */
	size_t rsnie_p_len;
	u32 lifetime;
	int cipher; /* Selected cipher (WPA_CIPHER_*) */
	u8 dtoken;

	struct tpk {
		u8 kck[16]; /* TPK-KCK */
		u8 tk[16]; /* TPK-TK; assuming only CCMP will be used */
	} tpk;
	int tpk_set;
	int tk_set; /* TPK-TK configured to the driver */
	int tpk_success;
	int tpk_in_progress;

	struct tpk_timer {
		u8 dest[ETH_ALEN];
		int count;      /* Retry Count */
		int timer;      /* Timeout in milliseconds */
		u8 action_code; /* TDLS frame type */
		u8 dialog_token;
		u16 status_code;
		u32 peer_capab;
		int buf_len;    /* length of TPK message for retransmission */
		u8 *buf;        /* buffer for TPK message */
	} sm_tmr;

	u16 capability;

	u8 supp_rates[IEEE80211_MAX_SUPP_RATES];
	size_t supp_rates_len;

	struct ieee80211_ht_capabilities *ht_capabilities;
	struct ieee80211_vht_capabilities *vht_capabilities;

	u8 qos_info;

	u16 aid;

	u8 *ext_capab;
	size_t ext_capab_len;

	u8 *supp_channels;
	size_t supp_channels_len;

	u8 *supp_oper_classes;
	size_t supp_oper_classes_len;

	u8 wmm_capable;

	/* channel switch currently enabled */
	int chan_switch_enabled;
};


static int wpa_tdls_get_privacy(struct wpa_sm *sm)
{
	/*
	 * Get info needed from supplicant to check if the current BSS supports
	 * security. Other than OPEN mode, rest are considered secured
	 * WEP/WPA/WPA2 hence TDLS frames are processed for TPK handshake.
	 */
	return sm->pairwise_cipher != WPA_CIPHER_NONE;
}


static u8 * wpa_add_ie(u8 *pos, const u8 *ie, size_t ie_len)
{
	os_memcpy(pos, ie, ie_len);
	return pos + ie_len;
}


static int wpa_tdls_del_key(struct wpa_sm *sm, struct wpa_tdls_peer *peer)
{
	if (wpa_sm_set_key(sm, WPA_ALG_NONE, peer->addr,
			   0, 0, NULL, 0, NULL, 0) < 0) {
		wpa_printf(MSG_WARNING, "TDLS: Failed to delete TPK-TK from "
			   "the driver");
		return -1;
	}

	return 0;
}


static int wpa_tdls_set_key(struct wpa_sm *sm, struct wpa_tdls_peer *peer)
{
	u8 key_len;
	u8 rsc[6];
	enum wpa_alg alg;

	if (peer->tk_set) {
		/*
		 * This same TPK-TK has already been configured to the driver
		 * and this new configuration attempt (likely due to an
		 * unexpected retransmitted frame) would result in clearing
		 * the TX/RX sequence number which can break security, so must
		 * not allow that to happen.
		 */
		wpa_printf(MSG_INFO, "TDLS: TPK-TK for the peer " MACSTR
			   " has already been configured to the driver - do not reconfigure",
			   MAC2STR(peer->addr));
		return -1;
	}

	os_memset(rsc, 0, 6);

	switch (peer->cipher) {
	case WPA_CIPHER_CCMP:
		alg = WPA_ALG_CCMP;
		key_len = 16;
		break;
	case WPA_CIPHER_NONE:
		wpa_printf(MSG_DEBUG, "TDLS: Pairwise Cipher Suite: "
			   "NONE - do not use pairwise keys");
		return -1;
	default:
		wpa_printf(MSG_WARNING, "TDLS: Unsupported pairwise cipher %d",
			   sm->pairwise_cipher);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "TDLS: Configure pairwise key for peer " MACSTR,
		   MAC2STR(peer->addr));
	if (wpa_sm_set_key(sm, alg, peer->addr, -1, 1,
			   rsc, sizeof(rsc), peer->tpk.tk, key_len) < 0) {
		wpa_printf(MSG_WARNING, "TDLS: Failed to set TPK to the "
			   "driver");
		return -1;
	}
	peer->tk_set = 1;
	return 0;
}


static int wpa_tdls_send_tpk_msg(struct wpa_sm *sm, const u8 *dst,
				 u8 action_code, u8 dialog_token,
				 u16 status_code, u32 peer_capab,
				 int initiator, const u8 *buf, size_t len)
{
	return wpa_sm_send_tdls_mgmt(sm, dst, action_code, dialog_token,
				     status_code, peer_capab, initiator, buf,
				     len);
}


static int wpa_tdls_tpk_send(struct wpa_sm *sm, const u8 *dest, u8 action_code,
			     u8 dialog_token, u16 status_code, u32 peer_capab,
			     int initiator, const u8 *msg, size_t msg_len)
{
	struct wpa_tdls_peer *peer;

	wpa_printf(MSG_DEBUG, "TDLS: TPK send dest=" MACSTR " action_code=%u "
		   "dialog_token=%u status_code=%u peer_capab=%u initiator=%d "
		   "msg_len=%u",
		   MAC2STR(dest), action_code, dialog_token, status_code,
		   peer_capab, initiator, (unsigned int) msg_len);

	if (wpa_tdls_send_tpk_msg(sm, dest, action_code, dialog_token,
				  status_code, peer_capab, initiator, msg,
				  msg_len)) {
		wpa_printf(MSG_INFO, "TDLS: Failed to send message "
			   "(action_code=%u)", action_code);
		return -1;
	}

	if (action_code == WLAN_TDLS_SETUP_CONFIRM ||
	    action_code == WLAN_TDLS_TEARDOWN ||
	    action_code == WLAN_TDLS_DISCOVERY_REQUEST ||
	    action_code == WLAN_TDLS_DISCOVERY_RESPONSE)
		return 0; /* No retries */

	for (peer = sm->tdls; peer; peer = peer->next) {
		if (os_memcmp(peer->addr, dest, ETH_ALEN) == 0)
			break;
	}

	if (peer == NULL) {
		wpa_printf(MSG_INFO, "TDLS: No matching entry found for "
			   "retry " MACSTR, MAC2STR(dest));
		return 0;
	}

	eloop_cancel_timeout(wpa_tdls_tpk_retry_timeout, sm, peer);

	if (action_code == WLAN_TDLS_SETUP_RESPONSE) {
		peer->sm_tmr.count = TPK_M2_RETRY_COUNT;
		peer->sm_tmr.timer = TPK_M2_TIMEOUT;
	} else {
		peer->sm_tmr.count = TPK_M1_RETRY_COUNT;
		peer->sm_tmr.timer = TPK_M1_TIMEOUT;
	}

	/* Copy message to resend on timeout */
	os_memcpy(peer->sm_tmr.dest, dest, ETH_ALEN);
	peer->sm_tmr.action_code = action_code;
	peer->sm_tmr.dialog_token = dialog_token;
	peer->sm_tmr.status_code = status_code;
	peer->sm_tmr.peer_capab = peer_capab;
	peer->sm_tmr.buf_len = msg_len;
	os_free(peer->sm_tmr.buf);
	peer->sm_tmr.buf = os_memdup(msg, msg_len);
	if (peer->sm_tmr.buf == NULL)
		return -1;

	wpa_printf(MSG_DEBUG, "TDLS: Retry timeout registered "
		   "(action_code=%u)", action_code);
	eloop_register_timeout(peer->sm_tmr.timer / 1000,
			       (peer->sm_tmr.timer % 1000) * 1000,
			       wpa_tdls_tpk_retry_timeout, sm, peer);
	return 0;
}


static int wpa_tdls_do_teardown(struct wpa_sm *sm, struct wpa_tdls_peer *peer,
				u16 reason_code)
{
	int ret;

	ret = wpa_tdls_send_teardown(sm, peer->addr, reason_code);
	/* disable the link after teardown was sent */
	wpa_tdls_disable_peer_link(sm, peer);

	return ret;
}


static void wpa_tdls_tpk_retry_timeout(void *eloop_ctx, void *timeout_ctx)
{

	struct wpa_sm *sm = eloop_ctx;
	struct wpa_tdls_peer *peer = timeout_ctx;

	if (peer->sm_tmr.count) {
		peer->sm_tmr.count--;

		wpa_printf(MSG_INFO, "TDLS: Retrying sending of message "
			   "(action_code=%u)",
			   peer->sm_tmr.action_code);

		if (peer->sm_tmr.buf == NULL) {
			wpa_printf(MSG_INFO, "TDLS: No retry buffer available "
				   "for action_code=%u",
				   peer->sm_tmr.action_code);
			eloop_cancel_timeout(wpa_tdls_tpk_retry_timeout, sm,
					     peer);
			return;
		}

		/* resend TPK Handshake Message to Peer */
		if (wpa_tdls_send_tpk_msg(sm, peer->sm_tmr.dest,
					  peer->sm_tmr.action_code,
					  peer->sm_tmr.dialog_token,
					  peer->sm_tmr.status_code,
					  peer->sm_tmr.peer_capab,
					  peer->initiator,
					  peer->sm_tmr.buf,
					  peer->sm_tmr.buf_len)) {
			wpa_printf(MSG_INFO, "TDLS: Failed to retry "
				   "transmission");
		}

		eloop_cancel_timeout(wpa_tdls_tpk_retry_timeout, sm, peer);
		eloop_register_timeout(peer->sm_tmr.timer / 1000,
				       (peer->sm_tmr.timer % 1000) * 1000,
				       wpa_tdls_tpk_retry_timeout, sm, peer);
	} else {
		eloop_cancel_timeout(wpa_tdls_tpk_retry_timeout, sm, peer);

		wpa_printf(MSG_DEBUG, "TDLS: Sending Teardown Request");
		wpa_tdls_do_teardown(sm, peer,
				     WLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED);
	}
}


static void wpa_tdls_tpk_retry_timeout_cancel(struct wpa_sm *sm,
					      struct wpa_tdls_peer *peer,
					      u8 action_code)
{
	if (action_code == peer->sm_tmr.action_code) {
		wpa_printf(MSG_DEBUG, "TDLS: Retry timeout cancelled for "
			   "action_code=%u", action_code);

		/* Cancel Timeout registered */
		eloop_cancel_timeout(wpa_tdls_tpk_retry_timeout, sm, peer);

		/* free all resources meant for retry */
		os_free(peer->sm_tmr.buf);
		peer->sm_tmr.buf = NULL;

		peer->sm_tmr.count = 0;
		peer->sm_tmr.timer = 0;
		peer->sm_tmr.buf_len = 0;
		peer->sm_tmr.action_code = 0xff;
	} else {
		wpa_printf(MSG_INFO, "TDLS: Error in cancelling retry timeout "
			   "(Unknown action_code=%u)", action_code);
	}
}


static void wpa_tdls_generate_tpk(struct wpa_tdls_peer *peer,
				  const u8 *own_addr, const u8 *bssid)
{
	u8 key_input[SHA256_MAC_LEN];
	const u8 *nonce[2];
	size_t len[2];
	u8 data[3 * ETH_ALEN];

	/* IEEE Std 802.11-2016 12.7.9.2:
	 * TPK-Key-Input = Hash(min(SNonce, ANonce) || max(SNonce, ANonce))
	 * Hash = SHA-256 for TDLS
	 */
	len[0] = WPA_NONCE_LEN;
	len[1] = WPA_NONCE_LEN;
	if (os_memcmp(peer->inonce, peer->rnonce, WPA_NONCE_LEN) < 0) {
		nonce[0] = peer->inonce;
		nonce[1] = peer->rnonce;
	} else {
		nonce[0] = peer->rnonce;
		nonce[1] = peer->inonce;
	}
	wpa_hexdump(MSG_DEBUG, "TDLS: min(Nonce)", nonce[0], WPA_NONCE_LEN);
	wpa_hexdump(MSG_DEBUG, "TDLS: max(Nonce)", nonce[1], WPA_NONCE_LEN);
	sha256_vector(2, nonce, len, key_input);
	wpa_hexdump_key(MSG_DEBUG, "TDLS: TPK-Key-Input",
			key_input, SHA256_MAC_LEN);

	/*
	 * TPK = KDF-Hash-Length(TPK-Key-Input, "TDLS PMK",
	 *	min(MAC_I, MAC_R) || max(MAC_I, MAC_R) || BSSID)
	 */

	if (os_memcmp(own_addr, peer->addr, ETH_ALEN) < 0) {
		os_memcpy(data, own_addr, ETH_ALEN);
		os_memcpy(data + ETH_ALEN, peer->addr, ETH_ALEN);
	} else {
		os_memcpy(data, peer->addr, ETH_ALEN);
		os_memcpy(data + ETH_ALEN, own_addr, ETH_ALEN);
	}
	os_memcpy(data + 2 * ETH_ALEN, bssid, ETH_ALEN);
	wpa_hexdump(MSG_DEBUG, "TDLS: KDF Context", data, sizeof(data));

	sha256_prf(key_input, SHA256_MAC_LEN, "TDLS PMK", data, sizeof(data),
		   (u8 *) &peer->tpk, sizeof(peer->tpk));
	wpa_hexdump_key(MSG_DEBUG, "TDLS: TPK-KCK",
			peer->tpk.kck, sizeof(peer->tpk.kck));
	wpa_hexdump_key(MSG_DEBUG, "TDLS: TPK-TK",
			peer->tpk.tk, sizeof(peer->tpk.tk));
	peer->tpk_set = 1;
}


/**
 * wpa_tdls_ftie_mic - Calculate TDLS FTIE MIC
 * @kck: TPK-KCK
 * @lnkid: Pointer to the beginning of Link Identifier IE
 * @rsnie: Pointer to the beginning of RSN IE used for handshake
 * @timeoutie: Pointer to the beginning of Timeout IE used for handshake
 * @ftie: Pointer to the beginning of FT IE
 * @mic: Pointer for writing MIC
 *
 * Calculate MIC for TDLS frame.
 */
static int wpa_tdls_ftie_mic(const u8 *kck, u8 trans_seq, const u8 *lnkid,
			     const u8 *rsnie, const u8 *timeoutie,
			     const u8 *ftie, u8 *mic)
{
	u8 *buf, *pos;
	struct wpa_tdls_ftie *_ftie;
	const struct wpa_tdls_lnkid *_lnkid;
	int ret;
	int len = 2 * ETH_ALEN + 1 + 2 + lnkid[1] + 2 + rsnie[1] +
		2 + timeoutie[1] + 2 + ftie[1];
	buf = os_zalloc(len);
	if (!buf) {
		wpa_printf(MSG_WARNING, "TDLS: No memory for MIC calculation");
		return -1;
	}

	pos = buf;
	_lnkid = (const struct wpa_tdls_lnkid *) lnkid;
	/* 1) TDLS initiator STA MAC address */
	os_memcpy(pos, _lnkid->init_sta, ETH_ALEN);
	pos += ETH_ALEN;
	/* 2) TDLS responder STA MAC address */
	os_memcpy(pos, _lnkid->resp_sta, ETH_ALEN);
	pos += ETH_ALEN;
	/* 3) Transaction Sequence number */
	*pos++ = trans_seq;
	/* 4) Link Identifier IE */
	os_memcpy(pos, lnkid, 2 + lnkid[1]);
	pos += 2 + lnkid[1];
	/* 5) RSN IE */
	os_memcpy(pos, rsnie, 2 + rsnie[1]);
	pos += 2 + rsnie[1];
	/* 6) Timeout Interval IE */
	os_memcpy(pos, timeoutie, 2 + timeoutie[1]);
	pos += 2 + timeoutie[1];
	/* 7) FTIE, with the MIC field of the FTIE set to 0 */
	os_memcpy(pos, ftie, 2 + ftie[1]);
	_ftie = (struct wpa_tdls_ftie *) pos;
	os_memset(_ftie->mic, 0, TDLS_MIC_LEN);
	pos += 2 + ftie[1];

	wpa_hexdump(MSG_DEBUG, "TDLS: Data for FTIE MIC", buf, pos - buf);
	wpa_hexdump_key(MSG_DEBUG, "TDLS: KCK", kck, 16);
	ret = omac1_aes_128(kck, buf, pos - buf, mic);
	os_free(buf);
	wpa_hexdump(MSG_DEBUG, "TDLS: FTIE MIC", mic, 16);
	return ret;
}


/**
 * wpa_tdls_key_mic_teardown - Calculate TDLS FTIE MIC for Teardown frame
 * @kck: TPK-KCK
 * @trans_seq: Transaction Sequence Number (4 - Teardown)
 * @rcode: Reason code for Teardown
 * @dtoken: Dialog Token used for that particular link
 * @lnkid: Pointer to the beginning of Link Identifier IE
 * @ftie: Pointer to the beginning of FT IE
 * @mic: Pointer for writing MIC
 *
 * Calculate MIC for TDLS frame.
 */
static int wpa_tdls_key_mic_teardown(const u8 *kck, u8 trans_seq, u16 rcode,
				     u8 dtoken, const u8 *lnkid,
				     const u8 *ftie, u8 *mic)
{
	u8 *buf, *pos;
	struct wpa_tdls_ftie *_ftie;
	int ret;
	int len;

	if (lnkid == NULL)
		return -1;

	len = 2 + lnkid[1] + sizeof(rcode) + sizeof(dtoken) +
		sizeof(trans_seq) + 2 + ftie[1];

	buf = os_zalloc(len);
	if (!buf) {
		wpa_printf(MSG_WARNING, "TDLS: No memory for MIC calculation");
		return -1;
	}

	pos = buf;
	/* 1) Link Identifier IE */
	os_memcpy(pos, lnkid, 2 + lnkid[1]);
	pos += 2 + lnkid[1];
	/* 2) Reason Code */
	WPA_PUT_LE16(pos, rcode);
	pos += sizeof(rcode);
	/* 3) Dialog token */
	*pos++ = dtoken;
	/* 4) Transaction Sequence number */
	*pos++ = trans_seq;
	/* 7) FTIE, with the MIC field of the FTIE set to 0 */
	os_memcpy(pos, ftie, 2 + ftie[1]);
	_ftie = (struct wpa_tdls_ftie *) pos;
	os_memset(_ftie->mic, 0, TDLS_MIC_LEN);
	pos += 2 + ftie[1];

	wpa_hexdump(MSG_DEBUG, "TDLS: Data for FTIE MIC", buf, pos - buf);
	wpa_hexdump_key(MSG_DEBUG, "TDLS: KCK", kck, 16);
	ret = omac1_aes_128(kck, buf, pos - buf, mic);
	os_free(buf);
	wpa_hexdump(MSG_DEBUG, "TDLS: FTIE MIC", mic, 16);
	return ret;
}


static int wpa_supplicant_verify_tdls_mic(u8 trans_seq,
					  struct wpa_tdls_peer *peer,
					  const u8 *lnkid, const u8 *timeoutie,
					  const struct wpa_tdls_ftie *ftie)
{
	u8 mic[16];

	if (peer->tpk_set) {
		wpa_tdls_ftie_mic(peer->tpk.kck, trans_seq, lnkid,
				  peer->rsnie_p, timeoutie, (u8 *) ftie,
				  mic);
		if (os_memcmp_const(mic, ftie->mic, 16) != 0) {
			wpa_printf(MSG_INFO, "TDLS: Invalid MIC in FTIE - "
				   "dropping packet");
			wpa_hexdump(MSG_DEBUG, "TDLS: Received MIC",
				    ftie->mic, 16);
			wpa_hexdump(MSG_DEBUG, "TDLS: Calculated MIC",
				    mic, 16);
			return -1;
		}
	} else {
		wpa_printf(MSG_WARNING, "TDLS: Could not verify TDLS MIC, "
			   "TPK not set - dropping packet");
		return -1;
	}
	return 0;
}


static int wpa_supplicant_verify_tdls_mic_teardown(
	u8 trans_seq, u16 rcode, u8 dtoken, struct wpa_tdls_peer *peer,
	const u8 *lnkid, const struct wpa_tdls_ftie *ftie)
{
	u8 mic[16];

	if (peer->tpk_set) {
		wpa_tdls_key_mic_teardown(peer->tpk.kck, trans_seq, rcode,
					  dtoken, lnkid, (u8 *) ftie, mic);
		if (os_memcmp_const(mic, ftie->mic, 16) != 0) {
			wpa_printf(MSG_INFO, "TDLS: Invalid MIC in Teardown - "
				   "dropping packet");
			return -1;
		}
	} else {
		wpa_printf(MSG_INFO, "TDLS: Could not verify TDLS Teardown "
			   "MIC, TPK not set - dropping packet");
		return -1;
	}
	return 0;
}


static void wpa_tdls_tpk_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_sm *sm = eloop_ctx;
	struct wpa_tdls_peer *peer = timeout_ctx;

	/*
	 * On TPK lifetime expiration, we have an option of either tearing down
	 * the direct link or trying to re-initiate it. The selection of what
	 * to do is not strictly speaking controlled by our role in the expired
	 * link, but for now, use that to select whether to renew or tear down
	 * the link.
	 */

	if (peer->initiator) {
		u8 addr[ETH_ALEN];

		wpa_printf(MSG_DEBUG, "TDLS: TPK lifetime expired for " MACSTR
			   " - try to renew", MAC2STR(peer->addr));
		/* cache the peer address before do_teardown */
		os_memcpy(addr, peer->addr, ETH_ALEN);
		wpa_tdls_do_teardown(sm, peer,
				     WLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED);
		wpa_tdls_start(sm, addr);
	} else {
		wpa_printf(MSG_DEBUG, "TDLS: TPK lifetime expired for " MACSTR
			   " - tear down", MAC2STR(peer->addr));
		wpa_tdls_do_teardown(sm, peer,
				     WLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED);
	}
}


static void wpa_tdls_peer_remove_from_list(struct wpa_sm *sm,
					   struct wpa_tdls_peer *peer)
{
	struct wpa_tdls_peer *cur, *prev;

	cur = sm->tdls;
	prev = NULL;
	while (cur && cur != peer) {
		prev = cur;
		cur = cur->next;
	}

	if (cur != peer) {
		wpa_printf(MSG_ERROR, "TDLS: Could not find peer " MACSTR
			   " to remove it from the list",
			   MAC2STR(peer->addr));
		return;
	}

	if (prev)
		prev->next = peer->next;
	else
		sm->tdls = peer->next;
}


static void wpa_tdls_peer_clear(struct wpa_sm *sm, struct wpa_tdls_peer *peer)
{
	wpa_printf(MSG_DEBUG, "TDLS: Clear state for peer " MACSTR,
		   MAC2STR(peer->addr));
	eloop_cancel_timeout(wpa_tdls_tpk_timeout, sm, peer);
	eloop_cancel_timeout(wpa_tdls_tpk_retry_timeout, sm, peer);
	peer->reconfig_key = 0;
	peer->initiator = 0;
	peer->tpk_in_progress = 0;
	os_free(peer->sm_tmr.buf);
	peer->sm_tmr.buf = NULL;
	os_free(peer->ht_capabilities);
	peer->ht_capabilities = NULL;
	os_free(peer->vht_capabilities);
	peer->vht_capabilities = NULL;
	os_free(peer->ext_capab);
	peer->ext_capab = NULL;
	os_free(peer->supp_channels);
	peer->supp_channels = NULL;
	os_free(peer->supp_oper_classes);
	peer->supp_oper_classes = NULL;
	peer->rsnie_i_len = peer->rsnie_p_len = 0;
	peer->cipher = 0;
	peer->qos_info = 0;
	peer->wmm_capable = 0;
	peer->tk_set = peer->tpk_set = peer->tpk_success = 0;
	peer->chan_switch_enabled = 0;
	os_memset(&peer->tpk, 0, sizeof(peer->tpk));
	os_memset(peer->inonce, 0, WPA_NONCE_LEN);
	os_memset(peer->rnonce, 0, WPA_NONCE_LEN);
}


static void wpa_tdls_peer_free(struct wpa_sm *sm, struct wpa_tdls_peer *peer)
{
	wpa_tdls_peer_clear(sm, peer);
	wpa_tdls_peer_remove_from_list(sm, peer);
	os_free(peer);
}


static void wpa_tdls_linkid(struct wpa_sm *sm, struct wpa_tdls_peer *peer,
			    struct wpa_tdls_lnkid *lnkid)
{
	lnkid->ie_type = WLAN_EID_LINK_ID;
	lnkid->ie_len = 3 * ETH_ALEN;
	os_memcpy(lnkid->bssid, sm->bssid, ETH_ALEN);
	if (peer->initiator) {
		os_memcpy(lnkid->init_sta, sm->own_addr, ETH_ALEN);
		os_memcpy(lnkid->resp_sta, peer->addr, ETH_ALEN);
	} else {
		os_memcpy(lnkid->init_sta, peer->addr, ETH_ALEN);
		os_memcpy(lnkid->resp_sta, sm->own_addr, ETH_ALEN);
	}
}


static int wpa_tdls_send_teardown(struct wpa_sm *sm, const u8 *addr,
				  u16 reason_code)
{
	struct wpa_tdls_peer *peer;
	struct wpa_tdls_ftie *ftie;
	struct wpa_tdls_lnkid lnkid;
	u8 dialog_token;
	u8 *rbuf, *pos;
	int ielen;

	if (sm->tdls_disabled || !sm->tdls_supported)
		return -1;

	/* Find the node and free from the list */
	for (peer = sm->tdls; peer; peer = peer->next) {
		if (os_memcmp(peer->addr, addr, ETH_ALEN) == 0)
			break;
	}

	if (peer == NULL) {
		wpa_printf(MSG_INFO, "TDLS: No matching entry found for "
			   "Teardown " MACSTR, MAC2STR(addr));
		return 0;
	}

	/* Cancel active channel switch before teardown */
	if (peer->chan_switch_enabled) {
		wpa_printf(MSG_DEBUG, "TDLS: First returning link with " MACSTR
			   " to base channel", MAC2STR(addr));
		wpa_sm_tdls_disable_channel_switch(sm, peer->addr);
	}

	dialog_token = peer->dtoken;

	wpa_printf(MSG_DEBUG, "TDLS: TDLS Teardown for " MACSTR,
		   MAC2STR(addr));

	ielen = 0;
	if (wpa_tdls_get_privacy(sm) && peer->tpk_set && peer->tpk_success) {
		/* To add FTIE for Teardown request and compute MIC */
		ielen += sizeof(*ftie);
#ifdef CONFIG_TDLS_TESTING
		if (tdls_testing & TDLS_TESTING_LONG_FRAME)
			ielen += 170;
#endif /* CONFIG_TDLS_TESTING */
	}

	rbuf = os_zalloc(ielen + 1);
	if (rbuf == NULL)
		return -1;
	pos = rbuf;

	if (!wpa_tdls_get_privacy(sm) || !peer->tpk_set || !peer->tpk_success)
		goto skip_ies;

	ftie = (struct wpa_tdls_ftie *) pos;
	ftie->ie_type = WLAN_EID_FAST_BSS_TRANSITION;
	/* Using the recent nonce which should be for CONFIRM frame */
	os_memcpy(ftie->Anonce, peer->rnonce, WPA_NONCE_LEN);
	os_memcpy(ftie->Snonce, peer->inonce, WPA_NONCE_LEN);
	ftie->ie_len = sizeof(struct wpa_tdls_ftie) - 2;
	pos = (u8 *) (ftie + 1);
#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_LONG_FRAME) {
		wpa_printf(MSG_DEBUG, "TDLS: Testing - add extra subelem to "
			   "FTIE");
		ftie->ie_len += 170;
		*pos++ = 255; /* FTIE subelem */
		*pos++ = 168; /* FTIE subelem length */
		pos += 168;
	}
#endif /* CONFIG_TDLS_TESTING */
	wpa_hexdump(MSG_DEBUG, "TDLS: FTIE for TDLS Teardown handshake",
		    (u8 *) ftie, pos - (u8 *) ftie);

	/* compute MIC before sending */
	wpa_tdls_linkid(sm, peer, &lnkid);
	wpa_tdls_key_mic_teardown(peer->tpk.kck, 4, reason_code,
				  dialog_token, (u8 *) &lnkid, (u8 *) ftie,
				  ftie->mic);

skip_ies:
	/* TODO: register for a Timeout handler, if Teardown is not received at
	 * the other end, then try again another time */

	/* request driver to send Teardown using this FTIE */
	wpa_tdls_tpk_send(sm, addr, WLAN_TDLS_TEARDOWN, 0,
			  reason_code, 0, peer->initiator, rbuf, pos - rbuf);
	os_free(rbuf);

	return 0;
}


int wpa_tdls_teardown_link(struct wpa_sm *sm, const u8 *addr, u16 reason_code)
{
	struct wpa_tdls_peer *peer;

	if (sm->tdls_disabled || !sm->tdls_supported)
		return -1;

	for (peer = sm->tdls; peer; peer = peer->next) {
		if (os_memcmp(peer->addr, addr, ETH_ALEN) == 0)
			break;
	}

	if (peer == NULL) {
		wpa_printf(MSG_DEBUG, "TDLS: Could not find peer " MACSTR
		   " for link Teardown", MAC2STR(addr));
		return -1;
	}

	if (!peer->tpk_success) {
		wpa_printf(MSG_DEBUG, "TDLS: Peer " MACSTR
		   " not connected - cannot Teardown link", MAC2STR(addr));
		return -1;
	}

	return wpa_tdls_do_teardown(sm, peer, reason_code);
}


static void wpa_tdls_disable_peer_link(struct wpa_sm *sm,
				       struct wpa_tdls_peer *peer)
{
	wpa_sm_tdls_oper(sm, TDLS_DISABLE_LINK, peer->addr);
	wpa_tdls_peer_free(sm, peer);
}


void wpa_tdls_disable_unreachable_link(struct wpa_sm *sm, const u8 *addr)
{
	struct wpa_tdls_peer *peer;

	for (peer = sm->tdls; peer; peer = peer->next) {
		if (os_memcmp(peer->addr, addr, ETH_ALEN) == 0)
			break;
	}

	if (!peer || !peer->tpk_success) {
		wpa_printf(MSG_DEBUG, "TDLS: Peer " MACSTR
			   " not connected - cannot teardown unreachable link",
			   MAC2STR(addr));
		return;
	}

	if (wpa_tdls_is_external_setup(sm)) {
		/*
		 * Get us on the base channel, disable the link, send a
		 * teardown packet through the AP, and then reset link data.
		 */
		if (peer->chan_switch_enabled)
			wpa_sm_tdls_disable_channel_switch(sm, peer->addr);
		wpa_sm_tdls_oper(sm, TDLS_DISABLE_LINK, addr);
		wpa_tdls_send_teardown(sm, addr,
				       WLAN_REASON_TDLS_TEARDOWN_UNREACHABLE);
		wpa_tdls_peer_free(sm, peer);
	} else {
		wpa_tdls_disable_peer_link(sm, peer);
	}
}


const char * wpa_tdls_get_link_status(struct wpa_sm *sm, const u8 *addr)
{
	struct wpa_tdls_peer *peer;

	if (sm->tdls_disabled || !sm->tdls_supported)
		return "disabled";

	for (peer = sm->tdls; peer; peer = peer->next) {
		if (os_memcmp(peer->addr, addr, ETH_ALEN) == 0)
			break;
	}

	if (peer == NULL)
		return "peer does not exist";

	if (!peer->tpk_success)
		return "peer not connected";

	return "connected";
}


static int wpa_tdls_recv_teardown(struct wpa_sm *sm, const u8 *src_addr,
				  const u8 *buf, size_t len)
{
	struct wpa_tdls_peer *peer = NULL;
	struct wpa_tdls_ftie *ftie;
	struct wpa_tdls_lnkid *lnkid;
	struct wpa_eapol_ie_parse kde;
	u16 reason_code;
	const u8 *pos;
	int ielen;

	/* Find the node and free from the list */
	for (peer = sm->tdls; peer; peer = peer->next) {
		if (os_memcmp(peer->addr, src_addr, ETH_ALEN) == 0)
			break;
	}

	if (peer == NULL) {
		wpa_printf(MSG_INFO, "TDLS: No matching entry found for "
			   "Teardown " MACSTR, MAC2STR(src_addr));
		return 0;
	}

	pos = buf;
	pos += 1 /* pkt_type */ + 1 /* Category */ + 1 /* Action */;

	reason_code = WPA_GET_LE16(pos);
	pos += 2;

	wpa_printf(MSG_DEBUG, "TDLS: TDLS Teardown Request from " MACSTR
		   " (reason code %u)", MAC2STR(src_addr), reason_code);

	ielen = len - (pos - buf); /* start of IE in buf */

	/*
	 * Don't reject the message if failing to parse IEs. The IEs we need are
	 * explicitly checked below. Some APs may add arbitrary padding to the
	 * end of short TDLS frames and that would look like invalid IEs.
	 */
	if (wpa_supplicant_parse_ies((const u8 *) pos, ielen, &kde) < 0)
		wpa_printf(MSG_DEBUG,
			   "TDLS: Failed to parse IEs in Teardown - ignore as an interop workaround");

	if (kde.lnkid == NULL || kde.lnkid_len < 3 * ETH_ALEN) {
		wpa_printf(MSG_INFO, "TDLS: No Link Identifier IE in TDLS "
			   "Teardown");
		return -1;
	}
	lnkid = (struct wpa_tdls_lnkid *) kde.lnkid;

	if (!wpa_tdls_get_privacy(sm) || !peer->tpk_set || !peer->tpk_success)
		goto skip_ftie;

	if (kde.ftie == NULL || kde.ftie_len < sizeof(*ftie)) {
		wpa_printf(MSG_INFO, "TDLS: No FTIE in TDLS Teardown");
		return -1;
	}

	ftie = (struct wpa_tdls_ftie *) kde.ftie;

	/* Process MIC check to see if TDLS Teardown is right */
	if (wpa_supplicant_verify_tdls_mic_teardown(4, reason_code,
						    peer->dtoken, peer,
						    (u8 *) lnkid, ftie) < 0) {
		wpa_printf(MSG_DEBUG, "TDLS: MIC failure for TDLS "
			   "Teardown Request from " MACSTR, MAC2STR(src_addr));
		return -1;
	}

skip_ftie:
	/*
	 * Request the driver to disable the direct link and clear associated
	 * keys.
	 */
	wpa_tdls_disable_peer_link(sm, peer);
	return 0;
}


/**
 * wpa_tdls_send_error - To send suitable TDLS status response with
 *	appropriate status code mentioning reason for error/failure.
 * @dst 	- MAC addr of Peer station
 * @tdls_action - TDLS frame type for which error code is sent
 * @initiator   - was this end the initiator of the connection
 * @status 	- status code mentioning reason
 */

static int wpa_tdls_send_error(struct wpa_sm *sm, const u8 *dst,
			       u8 tdls_action, u8 dialog_token, int initiator,
			       u16 status)
{
	wpa_printf(MSG_DEBUG, "TDLS: Sending error to " MACSTR
		   " (action=%u status=%u)",
		   MAC2STR(dst), tdls_action, status);
	return wpa_tdls_tpk_send(sm, dst, tdls_action, dialog_token, status,
				 0, initiator, NULL, 0);
}


static struct wpa_tdls_peer *
wpa_tdls_add_peer(struct wpa_sm *sm, const u8 *addr, int *existing)
{
	struct wpa_tdls_peer *peer;

	if (existing)
		*existing = 0;
	for (peer = sm->tdls; peer; peer = peer->next) {
		if (os_memcmp(peer->addr, addr, ETH_ALEN) == 0) {
			if (existing)
				*existing = 1;
			return peer; /* re-use existing entry */
		}
	}

	wpa_printf(MSG_INFO, "TDLS: Creating peer entry for " MACSTR,
		   MAC2STR(addr));

	peer = os_zalloc(sizeof(*peer));
	if (peer == NULL)
		return NULL;

	os_memcpy(peer->addr, addr, ETH_ALEN);
	peer->next = sm->tdls;
	sm->tdls = peer;

	return peer;
}


static int wpa_tdls_send_tpk_m1(struct wpa_sm *sm,
				struct wpa_tdls_peer *peer)
{
	size_t buf_len;
	struct wpa_tdls_timeoutie timeoutie;
	u16 rsn_capab;
	struct wpa_tdls_ftie *ftie;
	u8 *rbuf, *pos, *count_pos;
	u16 count;
	struct rsn_ie_hdr *hdr;
	int status;

	if (!wpa_tdls_get_privacy(sm)) {
		wpa_printf(MSG_DEBUG, "TDLS: No security used on the link");
		peer->rsnie_i_len = 0;
		goto skip_rsnie;
	}

	/*
	 * TPK Handshake Message 1:
	 * FTIE: ANonce=0, SNonce=initiator nonce MIC=0, DataKDs=(RSNIE_I,
	 * Timeout Interval IE))
	 */

	/* Filling RSN IE */
	hdr = (struct rsn_ie_hdr *) peer->rsnie_i;
	hdr->elem_id = WLAN_EID_RSN;
	WPA_PUT_LE16(hdr->version, RSN_VERSION);

	pos = (u8 *) (hdr + 1);
	RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_NO_GROUP_ADDRESSED);
	pos += RSN_SELECTOR_LEN;
	count_pos = pos;
	pos += 2;

	count = 0;

	/*
	 * AES-CCMP is the default Encryption preferred for TDLS, so
	 * RSN IE is filled only with CCMP CIPHER
	 * Note: TKIP is not used to encrypt TDLS link.
	 *
	 * Regardless of the cipher used on the AP connection, select CCMP
	 * here.
	 */
	RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_CCMP);
	pos += RSN_SELECTOR_LEN;
	count++;

	WPA_PUT_LE16(count_pos, count);

	WPA_PUT_LE16(pos, 1);
	pos += 2;
	RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_TPK_HANDSHAKE);
	pos += RSN_SELECTOR_LEN;

	rsn_capab = WPA_CAPABILITY_PEERKEY_ENABLED;
	rsn_capab |= RSN_NUM_REPLAY_COUNTERS_16 << 2;
#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_ALT_RSN_IE) {
		wpa_printf(MSG_DEBUG, "TDLS: Use alternative RSN IE for "
			   "testing");
		rsn_capab = WPA_CAPABILITY_PEERKEY_ENABLED;
	}
#endif /* CONFIG_TDLS_TESTING */
	WPA_PUT_LE16(pos, rsn_capab);
	pos += 2;
#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_ALT_RSN_IE) {
		/* Number of PMKIDs */
		*pos++ = 0x00;
		*pos++ = 0x00;
	}
#endif /* CONFIG_TDLS_TESTING */

	hdr->len = (pos - peer->rsnie_i) - 2;
	peer->rsnie_i_len = pos - peer->rsnie_i;
	wpa_hexdump(MSG_DEBUG, "TDLS: RSN IE for TPK handshake",
		    peer->rsnie_i, peer->rsnie_i_len);

skip_rsnie:
	buf_len = 0;
	if (wpa_tdls_get_privacy(sm))
		buf_len += peer->rsnie_i_len + sizeof(struct wpa_tdls_ftie) +
			sizeof(struct wpa_tdls_timeoutie);
#ifdef CONFIG_TDLS_TESTING
	if (wpa_tdls_get_privacy(sm) &&
	    (tdls_testing & TDLS_TESTING_LONG_FRAME))
		buf_len += 170;
	if (tdls_testing & TDLS_TESTING_DIFF_BSSID)
		buf_len += sizeof(struct wpa_tdls_lnkid);
#endif /* CONFIG_TDLS_TESTING */
	rbuf = os_zalloc(buf_len + 1);
	if (rbuf == NULL) {
		wpa_tdls_peer_free(sm, peer);
		return -1;
	}
	pos = rbuf;

	if (!wpa_tdls_get_privacy(sm))
		goto skip_ies;

	/* Initiator RSN IE */
	pos = wpa_add_ie(pos, peer->rsnie_i, peer->rsnie_i_len);

	ftie = (struct wpa_tdls_ftie *) pos;
	ftie->ie_type = WLAN_EID_FAST_BSS_TRANSITION;
	ftie->ie_len = sizeof(struct wpa_tdls_ftie) - 2;

	if (os_get_random(peer->inonce, WPA_NONCE_LEN)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_WARNING,
			"TDLS: Failed to get random data for initiator Nonce");
		os_free(rbuf);
		wpa_tdls_peer_free(sm, peer);
		return -1;
	}
	peer->tk_set = 0; /* A new nonce results in a new TK */
	wpa_hexdump(MSG_DEBUG, "TDLS: Initiator Nonce for TPK handshake",
		    peer->inonce, WPA_NONCE_LEN);
	os_memcpy(ftie->Snonce, peer->inonce, WPA_NONCE_LEN);

	wpa_hexdump(MSG_DEBUG, "TDLS: FTIE for TPK Handshake M1",
		    (u8 *) ftie, sizeof(struct wpa_tdls_ftie));

	pos = (u8 *) (ftie + 1);

#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_LONG_FRAME) {
		wpa_printf(MSG_DEBUG, "TDLS: Testing - add extra subelem to "
			   "FTIE");
		ftie->ie_len += 170;
		*pos++ = 255; /* FTIE subelem */
		*pos++ = 168; /* FTIE subelem length */
		pos += 168;
	}
#endif /* CONFIG_TDLS_TESTING */

	/* Lifetime */
	peer->lifetime = TPK_LIFETIME;
#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_SHORT_LIFETIME) {
		wpa_printf(MSG_DEBUG, "TDLS: Testing - use short TPK "
			   "lifetime");
		peer->lifetime = 301;
	}
	if (tdls_testing & TDLS_TESTING_LONG_LIFETIME) {
		wpa_printf(MSG_DEBUG, "TDLS: Testing - use long TPK "
			   "lifetime");
		peer->lifetime = 0xffffffff;
	}
#endif /* CONFIG_TDLS_TESTING */
	pos = wpa_add_tdls_timeoutie(pos, (u8 *) &timeoutie,
				     sizeof(timeoutie), peer->lifetime);
	wpa_printf(MSG_DEBUG, "TDLS: TPK lifetime %u seconds", peer->lifetime);

skip_ies:

#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_DIFF_BSSID) {
		struct wpa_tdls_lnkid *l = (struct wpa_tdls_lnkid *) pos;

		wpa_printf(MSG_DEBUG, "TDLS: Testing - use incorrect BSSID in "
			   "Link Identifier");
		wpa_tdls_linkid(sm, peer, l);
		l->bssid[5] ^= 0x01;
		pos += sizeof(*l);
	}
#endif /* CONFIG_TDLS_TESTING */

	wpa_printf(MSG_DEBUG, "TDLS: Sending TDLS Setup Request / TPK "
		   "Handshake Message 1 (peer " MACSTR ")",
		   MAC2STR(peer->addr));

	status = wpa_tdls_tpk_send(sm, peer->addr, WLAN_TDLS_SETUP_REQUEST,
				   1, 0, 0, peer->initiator, rbuf, pos - rbuf);
	os_free(rbuf);

	return status;
}


static int wpa_tdls_send_tpk_m2(struct wpa_sm *sm,
				const unsigned char *src_addr, u8 dtoken,
				struct wpa_tdls_lnkid *lnkid,
				const struct wpa_tdls_peer *peer)
{
	u8 *rbuf, *pos;
	size_t buf_len;
	u32 lifetime;
	struct wpa_tdls_timeoutie timeoutie;
	struct wpa_tdls_ftie *ftie;
	int status;

	buf_len = 0;
	if (wpa_tdls_get_privacy(sm)) {
		/* Peer RSN IE, FTIE(Initiator Nonce, Responder Nonce),
		 * Lifetime */
		buf_len += peer->rsnie_i_len + sizeof(struct wpa_tdls_ftie) +
			sizeof(struct wpa_tdls_timeoutie);
#ifdef CONFIG_TDLS_TESTING
		if (tdls_testing & TDLS_TESTING_LONG_FRAME)
			buf_len += 170;
#endif /* CONFIG_TDLS_TESTING */
	}

	rbuf = os_zalloc(buf_len + 1);
	if (rbuf == NULL)
		return -1;
	pos = rbuf;

	if (!wpa_tdls_get_privacy(sm))
		goto skip_ies;

	/* Peer RSN IE */
	pos = wpa_add_ie(pos, peer->rsnie_p, peer->rsnie_p_len);

	ftie = (struct wpa_tdls_ftie *) pos;
	ftie->ie_type = WLAN_EID_FAST_BSS_TRANSITION;
	/* TODO: ftie->mic_control to set 2-RESPONSE */
	os_memcpy(ftie->Anonce, peer->rnonce, WPA_NONCE_LEN);
	os_memcpy(ftie->Snonce, peer->inonce, WPA_NONCE_LEN);
	ftie->ie_len = sizeof(struct wpa_tdls_ftie) - 2;
	wpa_hexdump(MSG_DEBUG, "TDLS: FTIE for TPK M2",
		    (u8 *) ftie, sizeof(*ftie));

	pos = (u8 *) (ftie + 1);

#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_LONG_FRAME) {
		wpa_printf(MSG_DEBUG, "TDLS: Testing - add extra subelem to "
			   "FTIE");
		ftie->ie_len += 170;
		*pos++ = 255; /* FTIE subelem */
		*pos++ = 168; /* FTIE subelem length */
		pos += 168;
	}
#endif /* CONFIG_TDLS_TESTING */

	/* Lifetime */
	lifetime = peer->lifetime;
#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_WRONG_LIFETIME_RESP) {
		wpa_printf(MSG_DEBUG, "TDLS: Testing - use wrong TPK "
			   "lifetime in response");
		lifetime++;
	}
#endif /* CONFIG_TDLS_TESTING */
	pos = wpa_add_tdls_timeoutie(pos, (u8 *) &timeoutie,
				     sizeof(timeoutie), lifetime);
	wpa_printf(MSG_DEBUG, "TDLS: TPK lifetime %u seconds from initiator",
		   lifetime);

	/* compute MIC before sending */
	wpa_tdls_ftie_mic(peer->tpk.kck, 2, (u8 *) lnkid, peer->rsnie_p,
			  (u8 *) &timeoutie, (u8 *) ftie, ftie->mic);
#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_WRONG_MIC) {
		wpa_printf(MSG_DEBUG, "TDLS: Testing - use wrong MIC");
		ftie->mic[0] ^= 0x01;
	}
#endif /* CONFIG_TDLS_TESTING */

skip_ies:
	status = wpa_tdls_tpk_send(sm, src_addr, WLAN_TDLS_SETUP_RESPONSE,
				   dtoken, 0, 0, peer->initiator, rbuf,
				   pos - rbuf);
	os_free(rbuf);

	return status;
}


static int wpa_tdls_send_tpk_m3(struct wpa_sm *sm,
				const unsigned char *src_addr, u8 dtoken,
				struct wpa_tdls_lnkid *lnkid,
				const struct wpa_tdls_peer *peer)
{
	u8 *rbuf, *pos;
	size_t buf_len;
	struct wpa_tdls_ftie *ftie;
	struct wpa_tdls_timeoutie timeoutie;
	u32 lifetime;
	int status;
	u32 peer_capab = 0;

	buf_len = 0;
	if (wpa_tdls_get_privacy(sm)) {
		/* Peer RSN IE, FTIE(Initiator Nonce, Responder Nonce),
		 * Lifetime */
		buf_len += peer->rsnie_i_len + sizeof(struct wpa_tdls_ftie) +
			sizeof(struct wpa_tdls_timeoutie);
#ifdef CONFIG_TDLS_TESTING
		if (tdls_testing & TDLS_TESTING_LONG_FRAME)
			buf_len += 170;
#endif /* CONFIG_TDLS_TESTING */
	}

	rbuf = os_zalloc(buf_len + 1);
	if (rbuf == NULL)
		return -1;
	pos = rbuf;

	if (!wpa_tdls_get_privacy(sm))
		goto skip_ies;

	/* Peer RSN IE */
	pos = wpa_add_ie(pos, peer->rsnie_p, peer->rsnie_p_len);

	ftie = (struct wpa_tdls_ftie *) pos;
	ftie->ie_type = WLAN_EID_FAST_BSS_TRANSITION;
	/*TODO: ftie->mic_control to set 3-CONFIRM */
	os_memcpy(ftie->Anonce, peer->rnonce, WPA_NONCE_LEN);
	os_memcpy(ftie->Snonce, peer->inonce, WPA_NONCE_LEN);
	ftie->ie_len = sizeof(struct wpa_tdls_ftie) - 2;

	pos = (u8 *) (ftie + 1);

#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_LONG_FRAME) {
		wpa_printf(MSG_DEBUG, "TDLS: Testing - add extra subelem to "
			   "FTIE");
		ftie->ie_len += 170;
		*pos++ = 255; /* FTIE subelem */
		*pos++ = 168; /* FTIE subelem length */
		pos += 168;
	}
#endif /* CONFIG_TDLS_TESTING */

	/* Lifetime */
	lifetime = peer->lifetime;
#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_WRONG_LIFETIME_CONF) {
		wpa_printf(MSG_DEBUG, "TDLS: Testing - use wrong TPK "
			   "lifetime in confirm");
		lifetime++;
	}
#endif /* CONFIG_TDLS_TESTING */
	pos = wpa_add_tdls_timeoutie(pos, (u8 *) &timeoutie,
				     sizeof(timeoutie), lifetime);
	wpa_printf(MSG_DEBUG, "TDLS: TPK lifetime %u seconds",
		   lifetime);

	/* compute MIC before sending */
	wpa_tdls_ftie_mic(peer->tpk.kck, 3, (u8 *) lnkid, peer->rsnie_p,
			  (u8 *) &timeoutie, (u8 *) ftie, ftie->mic);
#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_WRONG_MIC) {
		wpa_printf(MSG_DEBUG, "TDLS: Testing - use wrong MIC");
		ftie->mic[0] ^= 0x01;
	}
#endif /* CONFIG_TDLS_TESTING */

skip_ies:

	if (peer->vht_capabilities)
		peer_capab |= TDLS_PEER_VHT;
	if (peer->ht_capabilities)
		peer_capab |= TDLS_PEER_HT;
	if (peer->wmm_capable)
		peer_capab |= TDLS_PEER_WMM;

	status = wpa_tdls_tpk_send(sm, src_addr, WLAN_TDLS_SETUP_CONFIRM,
				   dtoken, 0, peer_capab, peer->initiator,
				   rbuf, pos - rbuf);
	os_free(rbuf);

	return status;
}


static int wpa_tdls_send_discovery_response(struct wpa_sm *sm,
					    struct wpa_tdls_peer *peer,
					    u8 dialog_token)
{
	size_t buf_len = 0;
	struct wpa_tdls_timeoutie timeoutie;
	u16 rsn_capab;
	u8 *rbuf, *pos, *count_pos;
	u16 count;
	struct rsn_ie_hdr *hdr;
	int status;

	wpa_printf(MSG_DEBUG, "TDLS: Sending TDLS Discovery Response "
		   "(peer " MACSTR ")", MAC2STR(peer->addr));
	if (!wpa_tdls_get_privacy(sm))
		goto skip_rsn_ies;

	/* Filling RSN IE */
	hdr = (struct rsn_ie_hdr *) peer->rsnie_i;
	hdr->elem_id = WLAN_EID_RSN;
	WPA_PUT_LE16(hdr->version, RSN_VERSION);
	pos = (u8 *) (hdr + 1);
	RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_NO_GROUP_ADDRESSED);
	pos += RSN_SELECTOR_LEN;
	count_pos = pos;
	pos += 2;
	count = 0;

	/*
	* AES-CCMP is the default encryption preferred for TDLS, so
	* RSN IE is filled only with CCMP cipher suite.
	* Note: TKIP is not used to encrypt TDLS link.
	*
	* Regardless of the cipher used on the AP connection, select CCMP
	* here.
	*/
	RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_CCMP);
	pos += RSN_SELECTOR_LEN;
	count++;
	WPA_PUT_LE16(count_pos, count);
	WPA_PUT_LE16(pos, 1);
	pos += 2;
	RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_TPK_HANDSHAKE);
	pos += RSN_SELECTOR_LEN;

	rsn_capab = WPA_CAPABILITY_PEERKEY_ENABLED;
	rsn_capab |= RSN_NUM_REPLAY_COUNTERS_16 << 2;
	WPA_PUT_LE16(pos, rsn_capab);
	pos += 2;
	hdr->len = (pos - (u8 *) hdr) - 2;
	peer->rsnie_i_len = pos - peer->rsnie_i;

	wpa_hexdump(MSG_DEBUG, "TDLS: RSN IE for Discovery Response",
		    (u8 *) hdr, hdr->len + 2);
skip_rsn_ies:
	buf_len = 0;
	if (wpa_tdls_get_privacy(sm)) {
		/* Peer RSN IE, Lifetime */
		buf_len += peer->rsnie_i_len +
			sizeof(struct wpa_tdls_timeoutie);
	}
	rbuf = os_zalloc(buf_len + 1);
	if (rbuf == NULL) {
		wpa_tdls_peer_free(sm, peer);
		return -1;
	}
	pos = rbuf;

	if (!wpa_tdls_get_privacy(sm))
		goto skip_ies;
	/* Initiator RSN IE */
	pos = wpa_add_ie(pos, peer->rsnie_i, peer->rsnie_i_len);
	/* Lifetime */
	peer->lifetime = TPK_LIFETIME;
	pos = wpa_add_tdls_timeoutie(pos, (u8 *) &timeoutie,
				     sizeof(timeoutie), peer->lifetime);
	wpa_printf(MSG_DEBUG, "TDLS: TPK lifetime %u seconds", peer->lifetime);
skip_ies:
	status = wpa_tdls_tpk_send(sm, peer->addr, WLAN_TDLS_DISCOVERY_RESPONSE,
				   dialog_token, 0, 0, 0, rbuf, pos - rbuf);
	os_free(rbuf);

	return status;
}


static int
wpa_tdls_process_discovery_request(struct wpa_sm *sm, const u8 *addr,
				   const u8 *buf, size_t len)
{
	struct wpa_eapol_ie_parse kde;
	const struct wpa_tdls_lnkid *lnkid;
	struct wpa_tdls_peer *peer;
	size_t min_req_len = sizeof(struct wpa_tdls_frame) +
		1 /* dialog token */ + sizeof(struct wpa_tdls_lnkid);
	u8 dialog_token;

	wpa_printf(MSG_DEBUG, "TDLS: Discovery Request from " MACSTR,
		   MAC2STR(addr));

	if (len < min_req_len) {
		wpa_printf(MSG_DEBUG, "TDLS Discovery Request is too short: "
			   "%d", (int) len);
		return -1;
	}

	dialog_token = buf[sizeof(struct wpa_tdls_frame)];

	/*
	 * Some APs will tack on a weird IE to the end of a TDLS
	 * discovery request packet. This needn't fail the response,
	 * since the required IE are verified separately.
	 */
	if (wpa_supplicant_parse_ies(buf + sizeof(struct wpa_tdls_frame) + 1,
				     len - (sizeof(struct wpa_tdls_frame) + 1),
				     &kde) < 0) {
		wpa_printf(MSG_DEBUG,
			   "TDLS: Failed to parse IEs in Discovery Request - ignore as an interop workaround");
	}

	if (!kde.lnkid) {
		wpa_printf(MSG_DEBUG, "TDLS: Link ID not found in Discovery "
			   "Request");
		return -1;
	}

	lnkid = (const struct wpa_tdls_lnkid *) kde.lnkid;

	if (os_memcmp(sm->bssid, lnkid->bssid, ETH_ALEN) != 0) {
		wpa_printf(MSG_DEBUG, "TDLS: Discovery Request from different "
			   " BSS " MACSTR, MAC2STR(lnkid->bssid));
		return -1;
	}

	peer = wpa_tdls_add_peer(sm, addr, NULL);
	if (peer == NULL)
		return -1;

	return wpa_tdls_send_discovery_response(sm, peer, dialog_token);
}


int wpa_tdls_send_discovery_request(struct wpa_sm *sm, const u8 *addr)
{
	if (sm->tdls_disabled || !sm->tdls_supported)
		return -1;

	wpa_printf(MSG_DEBUG, "TDLS: Sending Discovery Request to peer "
		   MACSTR, MAC2STR(addr));
	return wpa_tdls_tpk_send(sm, addr, WLAN_TDLS_DISCOVERY_REQUEST,
				 1, 0, 0, 1, NULL, 0);
}


static int copy_supp_rates(const struct wpa_eapol_ie_parse *kde,
			   struct wpa_tdls_peer *peer)
{
	if (!kde->supp_rates) {
		wpa_printf(MSG_DEBUG, "TDLS: No supported rates received");
		return -1;
	}
	peer->supp_rates_len = merge_byte_arrays(
		peer->supp_rates, sizeof(peer->supp_rates),
		kde->supp_rates + 2, kde->supp_rates_len - 2,
		kde->ext_supp_rates ? kde->ext_supp_rates + 2 : NULL,
		kde->ext_supp_rates_len - 2);
	return 0;
}


static int copy_peer_ht_capab(const struct wpa_eapol_ie_parse *kde,
			      struct wpa_tdls_peer *peer)
{
	if (!kde->ht_capabilities) {
		wpa_printf(MSG_DEBUG, "TDLS: No supported ht capabilities "
			   "received");
		return 0;
	}

	if (!peer->ht_capabilities) {
		peer->ht_capabilities =
                        os_zalloc(sizeof(struct ieee80211_ht_capabilities));
		if (peer->ht_capabilities == NULL)
                        return -1;
	}

	os_memcpy(peer->ht_capabilities, kde->ht_capabilities,
                  sizeof(struct ieee80211_ht_capabilities));
	wpa_hexdump(MSG_DEBUG, "TDLS: Peer HT capabilities",
		    (u8 *) peer->ht_capabilities,
		    sizeof(struct ieee80211_ht_capabilities));

	return 0;
}


static int copy_peer_vht_capab(const struct wpa_eapol_ie_parse *kde,
			      struct wpa_tdls_peer *peer)
{
	if (!kde->vht_capabilities) {
		wpa_printf(MSG_DEBUG, "TDLS: No supported vht capabilities "
			   "received");
		return 0;
	}

	if (!peer->vht_capabilities) {
		peer->vht_capabilities =
                        os_zalloc(sizeof(struct ieee80211_vht_capabilities));
		if (peer->vht_capabilities == NULL)
                        return -1;
	}

	os_memcpy(peer->vht_capabilities, kde->vht_capabilities,
                  sizeof(struct ieee80211_vht_capabilities));
	wpa_hexdump(MSG_DEBUG, "TDLS: Peer VHT capabilities",
		    (u8 *) peer->vht_capabilities,
		    sizeof(struct ieee80211_vht_capabilities));

	return 0;
}


static int copy_peer_ext_capab(const struct wpa_eapol_ie_parse *kde,
			       struct wpa_tdls_peer *peer)
{
	if (!kde->ext_capab) {
		wpa_printf(MSG_DEBUG, "TDLS: No extended capabilities "
			   "received");
		return 0;
	}

	if (!peer->ext_capab || peer->ext_capab_len < kde->ext_capab_len - 2) {
		/* Need to allocate buffer to fit the new information */
		os_free(peer->ext_capab);
		peer->ext_capab = os_zalloc(kde->ext_capab_len - 2);
		if (peer->ext_capab == NULL)
			return -1;
	}

	peer->ext_capab_len = kde->ext_capab_len - 2;
	os_memcpy(peer->ext_capab, kde->ext_capab + 2, peer->ext_capab_len);

	return 0;
}


static int copy_peer_wmm_capab(const struct wpa_eapol_ie_parse *kde,
			       struct wpa_tdls_peer *peer)
{
	struct wmm_information_element *wmm;

	if (!kde->wmm) {
		wpa_printf(MSG_DEBUG, "TDLS: No supported WMM capabilities received");
		return 0;
	}

	if (kde->wmm_len < sizeof(struct wmm_information_element)) {
		wpa_printf(MSG_DEBUG, "TDLS: Invalid supported WMM capabilities received");
		return -1;
	}

	wmm = (struct wmm_information_element *) kde->wmm;
	peer->qos_info = wmm->qos_info;

	peer->wmm_capable = 1;

	wpa_printf(MSG_DEBUG, "TDLS: Peer WMM QOS Info 0x%x", peer->qos_info);
	return 0;
}


static int copy_peer_supp_channels(const struct wpa_eapol_ie_parse *kde,
				   struct wpa_tdls_peer *peer)
{
	if (!kde->supp_channels) {
		wpa_printf(MSG_DEBUG, "TDLS: No supported channels received");
		return 0;
	}

	if (!peer->supp_channels ||
	    peer->supp_channels_len < kde->supp_channels_len) {
		os_free(peer->supp_channels);
		peer->supp_channels = os_zalloc(kde->supp_channels_len);
		if (peer->supp_channels == NULL)
			return -1;
	}

	peer->supp_channels_len = kde->supp_channels_len;

	os_memcpy(peer->supp_channels, kde->supp_channels,
		  peer->supp_channels_len);
	wpa_hexdump(MSG_DEBUG, "TDLS: Peer Supported Channels",
		    (u8 *) peer->supp_channels, peer->supp_channels_len);
	return 0;
}


static int copy_peer_supp_oper_classes(const struct wpa_eapol_ie_parse *kde,
				       struct wpa_tdls_peer *peer)
{
	if (!kde->supp_oper_classes) {
		wpa_printf(MSG_DEBUG, "TDLS: No supported operating classes received");
		return 0;
	}

	if (!peer->supp_oper_classes ||
	    peer->supp_oper_classes_len < kde->supp_oper_classes_len) {
		os_free(peer->supp_oper_classes);
		peer->supp_oper_classes = os_zalloc(kde->supp_oper_classes_len);
		if (peer->supp_oper_classes == NULL)
			return -1;
	}

	peer->supp_oper_classes_len = kde->supp_oper_classes_len;
	os_memcpy(peer->supp_oper_classes, kde->supp_oper_classes,
		  peer->supp_oper_classes_len);
	wpa_hexdump(MSG_DEBUG, "TDLS: Peer Supported Operating Classes",
		    (u8 *) peer->supp_oper_classes,
		    peer->supp_oper_classes_len);
	return 0;
}


static int wpa_tdls_addset_peer(struct wpa_sm *sm, struct wpa_tdls_peer *peer,
				int add)
{
	return wpa_sm_tdls_peer_addset(sm, peer->addr, add, peer->aid,
				       peer->capability,
				       peer->supp_rates, peer->supp_rates_len,
				       peer->ht_capabilities,
				       peer->vht_capabilities,
				       peer->qos_info, peer->wmm_capable,
				       peer->ext_capab, peer->ext_capab_len,
				       peer->supp_channels,
				       peer->supp_channels_len,
				       peer->supp_oper_classes,
				       peer->supp_oper_classes_len);
}


static int tdls_nonce_set(const u8 *nonce)
{
	int i;

	for (i = 0; i < WPA_NONCE_LEN; i++) {
		if (nonce[i])
			return 1;
	}

	return 0;
}


static int wpa_tdls_process_tpk_m1(struct wpa_sm *sm, const u8 *src_addr,
				   const u8 *buf, size_t len)
{
	struct wpa_tdls_peer *peer;
	struct wpa_eapol_ie_parse kde;
	struct wpa_ie_data ie;
	int cipher;
	const u8 *cpos;
	struct wpa_tdls_ftie *ftie = NULL;
	struct wpa_tdls_timeoutie *timeoutie;
	struct wpa_tdls_lnkid *lnkid;
	u32 lifetime = 0;
#if 0
	struct rsn_ie_hdr *hdr;
	u8 *pos;
	u16 rsn_capab;
	u16 rsn_ver;
#endif
	u8 dtoken;
	u16 ielen;
	u16 status = WLAN_STATUS_UNSPECIFIED_FAILURE;
	int tdls_prohibited = sm->tdls_prohibited;
	int existing_peer = 0;

	if (len < 3 + 3)
		return -1;

	cpos = buf;
	cpos += 1 /* pkt_type */ + 1 /* Category */ + 1 /* Action */;

	/* driver had already verified the frame format */
	dtoken = *cpos++; /* dialog token */

	wpa_printf(MSG_INFO, "TDLS: Dialog Token in TPK M1 %d", dtoken);

	peer = wpa_tdls_add_peer(sm, src_addr, &existing_peer);
	if (peer == NULL)
		goto error;

	/* If found, use existing entry instead of adding a new one;
	 * how to handle the case where both ends initiate at the
	 * same time? */
	if (existing_peer) {
		if (peer->tpk_success) {
			wpa_printf(MSG_DEBUG, "TDLS: TDLS Setup Request while "
				   "direct link is enabled - tear down the "
				   "old link first");
			wpa_sm_tdls_oper(sm, TDLS_DISABLE_LINK, peer->addr);
			wpa_tdls_peer_clear(sm, peer);
		} else if (peer->initiator) {
			/*
			 * An entry is already present, so check if we already
			 * sent a TDLS Setup Request. If so, compare MAC
			 * addresses and let the STA with the lower MAC address
			 * continue as the initiator. The other negotiation is
			 * terminated.
			 */
			if (os_memcmp(sm->own_addr, src_addr, ETH_ALEN) < 0) {
				wpa_printf(MSG_DEBUG, "TDLS: Discard request "
					   "from peer with higher address "
					   MACSTR, MAC2STR(src_addr));
				return -1;
			} else {
				wpa_printf(MSG_DEBUG, "TDLS: Accept request "
					   "from peer with lower address "
					   MACSTR " (terminate previously "
					   "initiated negotiation",
					   MAC2STR(src_addr));
				wpa_sm_tdls_oper(sm, TDLS_DISABLE_LINK,
						 peer->addr);
				wpa_tdls_peer_clear(sm, peer);
			}
		}
	}

	/* capability information */
	peer->capability = WPA_GET_LE16(cpos);
	cpos += 2;

	ielen = len - (cpos - buf); /* start of IE in buf */

	/*
	 * Don't reject the message if failing to parse IEs. The IEs we need are
	 * explicitly checked below. Some APs may add arbitrary padding to the
	 * end of short TDLS frames and that would look like invalid IEs.
	 */
	if (wpa_supplicant_parse_ies(cpos, ielen, &kde) < 0)
		wpa_printf(MSG_DEBUG,
			   "TDLS: Failed to parse IEs in TPK M1 - ignore as an interop workaround");

	if (kde.lnkid == NULL || kde.lnkid_len < 3 * ETH_ALEN) {
		wpa_printf(MSG_INFO, "TDLS: No valid Link Identifier IE in "
			   "TPK M1");
		goto error;
	}
	wpa_hexdump(MSG_DEBUG, "TDLS: Link ID Received from TPK M1",
		    kde.lnkid, kde.lnkid_len);
	lnkid = (struct wpa_tdls_lnkid *) kde.lnkid;
	if (os_memcmp(sm->bssid, lnkid->bssid, ETH_ALEN) != 0) {
		wpa_printf(MSG_INFO, "TDLS: TPK M1 from diff BSS");
		status = WLAN_STATUS_REQUEST_DECLINED;
		goto error;
	}

	wpa_printf(MSG_DEBUG, "TDLS: TPK M1 - TPK initiator " MACSTR,
		   MAC2STR(src_addr));

	if (copy_supp_rates(&kde, peer) < 0)
		goto error;

	if (copy_peer_ht_capab(&kde, peer) < 0)
		goto error;

	if (copy_peer_vht_capab(&kde, peer) < 0)
		goto error;

	if (copy_peer_ext_capab(&kde, peer) < 0)
		goto error;

	if (copy_peer_supp_channels(&kde, peer) < 0)
		goto error;

	if (copy_peer_supp_oper_classes(&kde, peer) < 0)
		goto error;

	peer->qos_info = kde.qosinfo;

	/* Overwrite with the qos_info obtained in WMM IE */
	if (copy_peer_wmm_capab(&kde, peer) < 0)
		goto error;

	peer->aid = kde.aid;

#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_CONCURRENT_INIT) {
		peer = wpa_tdls_add_peer(sm, src_addr, NULL);
		if (peer == NULL)
			goto error;
		wpa_printf(MSG_DEBUG, "TDLS: Testing concurrent initiation of "
			   "TDLS setup - send own request");
		peer->initiator = 1;
		wpa_sm_tdls_peer_addset(sm, peer->addr, 1, 0, 0, NULL, 0, NULL,
					NULL, 0, 0, NULL, 0, NULL, 0, NULL, 0);
		wpa_tdls_send_tpk_m1(sm, peer);
	}

	if ((tdls_testing & TDLS_TESTING_IGNORE_AP_PROHIBIT) &&
	    tdls_prohibited) {
		wpa_printf(MSG_DEBUG, "TDLS: Testing - ignore AP prohibition "
			   "on TDLS");
		tdls_prohibited = 0;
	}
#endif /* CONFIG_TDLS_TESTING */

	if (tdls_prohibited) {
		wpa_printf(MSG_INFO, "TDLS: TDLS prohibited in this BSS");
		status = WLAN_STATUS_REQUEST_DECLINED;
		goto error;
	}

	if (!wpa_tdls_get_privacy(sm)) {
		if (kde.rsn_ie) {
			wpa_printf(MSG_INFO, "TDLS: RSN IE in TPK M1 while "
				   "security is disabled");
			status = WLAN_STATUS_SECURITY_DISABLED;
			goto error;
		}
		goto skip_rsn;
	}

	if (kde.ftie == NULL || kde.ftie_len < sizeof(*ftie) ||
	    kde.rsn_ie == NULL) {
		wpa_printf(MSG_INFO, "TDLS: No FTIE or RSN IE in TPK M1");
		status = WLAN_STATUS_INVALID_PARAMETERS;
		goto error;
	}

	if (kde.rsn_ie_len > TDLS_MAX_IE_LEN) {
		wpa_printf(MSG_INFO, "TDLS: Too long Initiator RSN IE in "
			   "TPK M1");
		status = WLAN_STATUS_INVALID_RSNIE;
		goto error;
	}

	if (wpa_parse_wpa_ie_rsn(kde.rsn_ie, kde.rsn_ie_len, &ie) < 0) {
		wpa_printf(MSG_INFO, "TDLS: Failed to parse RSN IE in TPK M1");
		status = WLAN_STATUS_INVALID_RSNIE;
		goto error;
	}

	cipher = ie.pairwise_cipher;
	if (cipher & WPA_CIPHER_CCMP) {
		wpa_printf(MSG_DEBUG, "TDLS: Using CCMP for direct link");
		cipher = WPA_CIPHER_CCMP;
	} else {
		wpa_printf(MSG_INFO, "TDLS: No acceptable cipher in TPK M1");
		status = WLAN_STATUS_PAIRWISE_CIPHER_NOT_VALID;
		goto error;
	}

	if ((ie.capabilities &
	     (WPA_CAPABILITY_NO_PAIRWISE | WPA_CAPABILITY_PEERKEY_ENABLED)) !=
	    WPA_CAPABILITY_PEERKEY_ENABLED) {
		wpa_printf(MSG_INFO, "TDLS: Invalid RSN Capabilities in "
			   "TPK M1");
		status = WLAN_STATUS_INVALID_RSN_IE_CAPAB;
		goto error;
	}

	/* Lifetime */
	if (kde.key_lifetime == NULL) {
		wpa_printf(MSG_INFO, "TDLS: No Key Lifetime IE in TPK M1");
		status = WLAN_STATUS_UNACCEPTABLE_LIFETIME;
		goto error;
	}
	timeoutie = (struct wpa_tdls_timeoutie *) kde.key_lifetime;
	lifetime = WPA_GET_LE32(timeoutie->value);
	wpa_printf(MSG_DEBUG, "TDLS: TPK lifetime %u seconds", lifetime);
	if (lifetime < 300) {
		wpa_printf(MSG_INFO, "TDLS: Too short TPK lifetime");
		status = WLAN_STATUS_UNACCEPTABLE_LIFETIME;
		goto error;
	}

skip_rsn:
#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_CONCURRENT_INIT) {
		if (os_memcmp(sm->own_addr, peer->addr, ETH_ALEN) < 0) {
			/*
			 * The request frame from us is going to win, so do not
			 * replace information based on this request frame from
			 * the peer.
			 */
			goto skip_rsn_check;
		}
	}
#endif /* CONFIG_TDLS_TESTING */

	peer->initiator = 0; /* Need to check */
	peer->dtoken = dtoken;

	if (!wpa_tdls_get_privacy(sm)) {
		peer->rsnie_i_len = 0;
		peer->rsnie_p_len = 0;
		peer->cipher = WPA_CIPHER_NONE;
		goto skip_rsn_check;
	}

	ftie = (struct wpa_tdls_ftie *) kde.ftie;
	os_memcpy(peer->rsnie_i, kde.rsn_ie, kde.rsn_ie_len);
	peer->rsnie_i_len = kde.rsn_ie_len;
	peer->cipher = cipher;

	if (os_memcmp(peer->inonce, ftie->Snonce, WPA_NONCE_LEN) != 0 ||
	    !tdls_nonce_set(peer->inonce)) {
		/*
		 * There is no point in updating the RNonce for every obtained
		 * TPK M1 frame (e.g., retransmission due to timeout) with the
		 * same INonce (SNonce in FTIE). However, if the TPK M1 is
		 * retransmitted with a different INonce, update the RNonce
		 * since this is for a new TDLS session.
		 */
		wpa_printf(MSG_DEBUG,
			   "TDLS: New TPK M1 INonce - generate new RNonce");
		os_memcpy(peer->inonce, ftie->Snonce, WPA_NONCE_LEN);
		if (os_get_random(peer->rnonce, WPA_NONCE_LEN)) {
			wpa_msg(sm->ctx->ctx, MSG_WARNING,
				"TDLS: Failed to get random data for responder nonce");
			goto error;
		}
		peer->tk_set = 0; /* A new nonce results in a new TK */
	}

#if 0
	/* get version info from RSNIE received from Peer */
	hdr = (struct rsn_ie_hdr *) kde.rsn_ie;
	rsn_ver = WPA_GET_LE16(hdr->version);

	/* use min(peer's version, out version) */
	if (rsn_ver > RSN_VERSION)
		rsn_ver = RSN_VERSION;

	hdr = (struct rsn_ie_hdr *) peer->rsnie_p;

	hdr->elem_id = WLAN_EID_RSN;
	WPA_PUT_LE16(hdr->version, rsn_ver);
	pos = (u8 *) (hdr + 1);

	RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_NO_GROUP_ADDRESSED);
	pos += RSN_SELECTOR_LEN;
	/* Include only the selected cipher in pairwise cipher suite */
	WPA_PUT_LE16(pos, 1);
	pos += 2;
	if (cipher == WPA_CIPHER_CCMP)
		RSN_SELECTOR_PUT(pos, RSN_CIPHER_SUITE_CCMP);
	pos += RSN_SELECTOR_LEN;

	WPA_PUT_LE16(pos, 1);
	pos += 2;
	RSN_SELECTOR_PUT(pos, RSN_AUTH_KEY_MGMT_TPK_HANDSHAKE);
	pos += RSN_SELECTOR_LEN;

	rsn_capab = WPA_CAPABILITY_PEERKEY_ENABLED;
	rsn_capab |= RSN_NUM_REPLAY_COUNTERS_16 << 2;
	WPA_PUT_LE16(pos, rsn_capab);
	pos += 2;

	hdr->len = (pos - peer->rsnie_p) - 2;
	peer->rsnie_p_len = pos - peer->rsnie_p;
#endif

	/* temp fix: validation of RSNIE later */
	os_memcpy(peer->rsnie_p, peer->rsnie_i, peer->rsnie_i_len);
	peer->rsnie_p_len = peer->rsnie_i_len;

	wpa_hexdump(MSG_DEBUG, "TDLS: RSN IE for TPK handshake",
		    peer->rsnie_p, peer->rsnie_p_len);

	peer->lifetime = lifetime;

	wpa_tdls_generate_tpk(peer, sm->own_addr, sm->bssid);

skip_rsn_check:
#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_CONCURRENT_INIT)
		goto skip_add_peer;
#endif /* CONFIG_TDLS_TESTING */

	/* add supported rates, capabilities, and qos_info to the TDLS peer */
	if (wpa_tdls_addset_peer(sm, peer, 1) < 0)
		goto error;

#ifdef CONFIG_TDLS_TESTING
skip_add_peer:
#endif /* CONFIG_TDLS_TESTING */
	peer->tpk_in_progress = 1;

	wpa_printf(MSG_DEBUG, "TDLS: Sending TDLS Setup Response / TPK M2");
	if (wpa_tdls_send_tpk_m2(sm, src_addr, dtoken, lnkid, peer) < 0) {
		wpa_sm_tdls_oper(sm, TDLS_DISABLE_LINK, peer->addr);
		goto error;
	}

#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_DOUBLE_TPK_M2) {
		wpa_printf(MSG_INFO, "TDLS: Testing - Send another TPK M2");
		wpa_tdls_send_tpk_m2(sm, src_addr, dtoken, lnkid, peer);
	}
#endif /* CONFIG_TDLS_TESTING */

	return 0;

error:
	wpa_tdls_send_error(sm, src_addr, WLAN_TDLS_SETUP_RESPONSE, dtoken, 0,
			    status);
	if (peer)
		wpa_tdls_peer_free(sm, peer);
	return -1;
}


static int wpa_tdls_enable_link(struct wpa_sm *sm, struct wpa_tdls_peer *peer)
{
	peer->tpk_success = 1;
	peer->tpk_in_progress = 0;
	eloop_cancel_timeout(wpa_tdls_tpk_timeout, sm, peer);
	if (wpa_tdls_get_privacy(sm)) {
		u32 lifetime = peer->lifetime;
		/*
		 * Start the initiator process a bit earlier to avoid race
		 * condition with the responder sending teardown request.
		 */
		if (lifetime > 3 && peer->initiator)
			lifetime -= 3;
		eloop_register_timeout(lifetime, 0, wpa_tdls_tpk_timeout,
				       sm, peer);
#ifdef CONFIG_TDLS_TESTING
		if (tdls_testing & TDLS_TESTING_NO_TPK_EXPIRATION) {
			wpa_printf(MSG_DEBUG,
				   "TDLS: Testing - disable TPK expiration");
			eloop_cancel_timeout(wpa_tdls_tpk_timeout, sm, peer);
		}
#endif /* CONFIG_TDLS_TESTING */
	}

	if (peer->reconfig_key && wpa_tdls_set_key(sm, peer) < 0) {
		wpa_printf(MSG_INFO, "TDLS: Could not configure key to the "
			   "driver");
		return -1;
	}
	peer->reconfig_key = 0;

	return wpa_sm_tdls_oper(sm, TDLS_ENABLE_LINK, peer->addr);
}


static int wpa_tdls_process_tpk_m2(struct wpa_sm *sm, const u8 *src_addr,
				   const u8 *buf, size_t len)
{
	struct wpa_tdls_peer *peer;
	struct wpa_eapol_ie_parse kde;
	struct wpa_ie_data ie;
	int cipher;
	struct wpa_tdls_ftie *ftie;
	struct wpa_tdls_timeoutie *timeoutie;
	struct wpa_tdls_lnkid *lnkid;
	u32 lifetime;
	u8 dtoken;
	int ielen;
	u16 status;
	const u8 *pos;
	int ret = 0;

	wpa_printf(MSG_DEBUG, "TDLS: Received TDLS Setup Response / TPK M2 "
		   "(Peer " MACSTR ")", MAC2STR(src_addr));
	for (peer = sm->tdls; peer; peer = peer->next) {
		if (os_memcmp(peer->addr, src_addr, ETH_ALEN) == 0)
			break;
	}
	if (peer == NULL) {
		wpa_printf(MSG_INFO, "TDLS: No matching peer found for "
			   "TPK M2: " MACSTR, MAC2STR(src_addr));
		return -1;
	}
	if (!peer->initiator) {
		/*
		 * This may happen if both devices try to initiate TDLS at the
		 * same time and we accept the TPK M1 from the peer in
		 * wpa_tdls_process_tpk_m1() and clear our previous state.
		 */
		wpa_printf(MSG_INFO, "TDLS: We were not the initiator, so "
			   "ignore TPK M2 from " MACSTR, MAC2STR(src_addr));
		return -1;
	}

	if (peer->tpk_success) {
		wpa_printf(MSG_INFO, "TDLS: Ignore incoming TPK M2 retry, from "
			   MACSTR " as TPK M3 was already sent",
			   MAC2STR(src_addr));
		return 0;
	}

	wpa_tdls_tpk_retry_timeout_cancel(sm, peer, WLAN_TDLS_SETUP_REQUEST);

	if (len < 3 + 2 + 1) {
		wpa_tdls_disable_peer_link(sm, peer);
		return -1;
	}

	pos = buf;
	pos += 1 /* pkt_type */ + 1 /* Category */ + 1 /* Action */;
	status = WPA_GET_LE16(pos);
	pos += 2 /* status code */;

	if (status != WLAN_STATUS_SUCCESS) {
		wpa_printf(MSG_INFO, "TDLS: Status code in TPK M2: %u",
			   status);
		wpa_tdls_disable_peer_link(sm, peer);
		return -1;
	}

	status = WLAN_STATUS_UNSPECIFIED_FAILURE;

	/* TODO: need to verify dialog token matches here or in kernel */
	dtoken = *pos++; /* dialog token */

	wpa_printf(MSG_DEBUG, "TDLS: Dialog Token in TPK M2 %d", dtoken);

	if (len < 3 + 2 + 1 + 2) {
		wpa_tdls_disable_peer_link(sm, peer);
		return -1;
	}

	/* capability information */
	peer->capability = WPA_GET_LE16(pos);
	pos += 2;

	ielen = len - (pos - buf); /* start of IE in buf */

	/*
	 * Don't reject the message if failing to parse IEs. The IEs we need are
	 * explicitly checked below. Some APs may add arbitrary padding to the
	 * end of short TDLS frames and that would look like invalid IEs.
	 */
	if (wpa_supplicant_parse_ies(pos, ielen, &kde) < 0)
		wpa_printf(MSG_DEBUG,
			   "TDLS: Failed to parse IEs in TPK M2 - ignore as an interop workaround");

#ifdef CONFIG_TDLS_TESTING
	if (tdls_testing & TDLS_TESTING_DECLINE_RESP) {
		wpa_printf(MSG_DEBUG, "TDLS: Testing - decline response");
		status = WLAN_STATUS_REQUEST_DECLINED;
		goto error;
	}
#endif /* CONFIG_TDLS_TESTING */

	if (kde.lnkid == NULL || kde.lnkid_len < 3 * ETH_ALEN) {
		wpa_printf(MSG_INFO, "TDLS: No valid Link Identifier IE in "
			   "TPK M2");
		goto error;
	}
	wpa_hexdump(MSG_DEBUG, "TDLS: Link ID Received from TPK M2",
		    kde.lnkid, kde.lnkid_len);
	lnkid = (struct wpa_tdls_lnkid *) kde.lnkid;

	if (os_memcmp(sm->bssid, lnkid->bssid, ETH_ALEN) != 0) {
		wpa_printf(MSG_INFO, "TDLS: TPK M2 from different BSS");
		status = WLAN_STATUS_NOT_IN_SAME_BSS;
		goto error;
	}

	if (copy_supp_rates(&kde, peer) < 0)
		goto error;

	if (copy_peer_ht_capab(&kde, peer) < 0)
		goto error;

	if (copy_peer_vht_capab(&kde, peer) < 0)
		goto error;

	if (copy_peer_ext_capab(&kde, peer) < 0)
		goto error;

	if (copy_peer_supp_channels(&kde, peer) < 0)
		goto error;

	if (copy_peer_supp_oper_classes(&kde, peer) < 0)
		goto error;

	peer->qos_info = kde.qosinfo;

	/* Overwrite with the qos_info obtained in WMM IE */
	if (copy_peer_wmm_capab(&kde, peer) < 0)
		goto error;

	peer->aid = kde.aid;

	if (!wpa_tdls_get_privacy(sm)) {
		peer->rsnie_p_len = 0;
		peer->cipher = WPA_CIPHER_NONE;
		goto skip_rsn;
	}

	if (kde.ftie == NULL || kde.ftie_len < sizeof(*ftie) ||
	    kde.rsn_ie == NULL) {
		wpa_printf(MSG_INFO, "TDLS: No FTIE or RSN IE in TPK M2");
		status = WLAN_STATUS_INVALID_PARAMETERS;
		goto error;
	}
	wpa_hexdump(MSG_DEBUG, "TDLS: RSN IE Received from TPK M2",
		    kde.rsn_ie, kde.rsn_ie_len);

	if (kde.rsn_ie_len > TDLS_MAX_IE_LEN) {
		wpa_printf(MSG_INFO,
			   "TDLS: Too long Responder RSN IE in TPK M2");
		status = WLAN_STATUS_INVALID_RSNIE;
		goto error;
	}

	/*
	 * FIX: bitwise comparison of RSN IE is not the correct way of
	 * validation this. It can be different, but certain fields must
	 * match. Since we list only a single pairwise cipher in TPK M1, the
	 * memcmp is likely to work in most cases, though.
	 */
	if (kde.rsn_ie_len != peer->rsnie_i_len ||
	    os_memcmp(peer->rsnie_i, kde.rsn_ie, peer->rsnie_i_len) != 0) {
		wpa_printf(MSG_INFO, "TDLS: RSN IE in TPK M2 does "
			   "not match with RSN IE used in TPK M1");
		wpa_hexdump(MSG_DEBUG, "TDLS: RSN IE Sent in TPK M1",
			    peer->rsnie_i, peer->rsnie_i_len);
		wpa_hexdump(MSG_DEBUG, "TDLS: RSN IE Received from TPK M2",
			    kde.rsn_ie, kde.rsn_ie_len);
		status = WLAN_STATUS_INVALID_RSNIE;
		goto error;
	}

	if (wpa_parse_wpa_ie_rsn(kde.rsn_ie, kde.rsn_ie_len, &ie) < 0) {
		wpa_printf(MSG_INFO, "TDLS: Failed to parse RSN IE in TPK M2");
		status = WLAN_STATUS_INVALID_RSNIE;
		goto error;
	}

	cipher = ie.pairwise_cipher;
	if (cipher == WPA_CIPHER_CCMP) {
		wpa_printf(MSG_DEBUG, "TDLS: Using CCMP for direct link");
		cipher = WPA_CIPHER_CCMP;
	} else {
		wpa_printf(MSG_INFO, "TDLS: No acceptable cipher in TPK M2");
		status = WLAN_STATUS_PAIRWISE_CIPHER_NOT_VALID;
		goto error;
	}

	wpa_hexdump(MSG_DEBUG, "TDLS: FTIE Received from TPK M2",
		    kde.ftie, sizeof(*ftie));
	ftie = (struct wpa_tdls_ftie *) kde.ftie;

	if (os_memcmp(peer->inonce, ftie->Snonce, WPA_NONCE_LEN) != 0) {
		wpa_printf(MSG_INFO, "TDLS: FTIE SNonce in TPK M2 does "
			   "not match with FTIE SNonce used in TPK M1");
		/* Silently discard the frame */
		return -1;
	}

	/* Responder Nonce and RSN IE */
	os_memcpy(peer->rnonce, ftie->Anonce, WPA_NONCE_LEN);
	os_memcpy(peer->rsnie_p, kde.rsn_ie, kde.rsn_ie_len);
	peer->rsnie_p_len = kde.rsn_ie_len;
	peer->cipher = cipher;

	/* Lifetime */
	if (kde.key_lifetime == NULL) {
		wpa_printf(MSG_INFO, "TDLS: No Key Lifetime IE in TPK M2");
		status = WLAN_STATUS_UNACCEPTABLE_LIFETIME;
		goto error;
	}
	timeoutie = (struct wpa_tdls_timeoutie *) kde.key_lifetime;
	lifetime = WPA_GET_LE32(timeoutie->value);
	wpa_printf(MSG_DEBUG, "TDLS: TPK lifetime %u seconds in TPK M2",
		   lifetime);
	if (lifetime != peer->lifetime) {
		wpa_printf(MSG_INFO, "TDLS: Unexpected TPK lifetime %u in "
			   "TPK M2 (expected %u)", lifetime, peer->lifetime);
		status = WLAN_STATUS_UNACCEPTABLE_LIFETIME;
		goto error;
	}

	wpa_tdls_generate_tpk(peer, sm->own_addr, sm->bssid);

	/* Process MIC check to see if TPK M2 is right */
	if (wpa_supplicant_verify_tdls_mic(2, peer, (u8 *) lnkid,
					   (u8 *) timeoutie, ftie) < 0) {
		/* Discard the frame */
		wpa_tdls_del_key(sm, peer);
		wpa_tdls_disable_peer_link(sm, peer);
		return -1;
	}

	if (wpa_tdls_set_key(sm, peer) < 0) {
		/*
		 * Some drivers may not be able to config the key prior to full
		 * STA entry having been configured.
		 */
		wpa_printf(MSG_DEBUG, "TDLS: Try to configure TPK again after "
			   "STA entry is complete");
		peer->reconfig_key = 1;
	}

skip_rsn:
	peer->dtoken = dtoken;

	/* add supported rates, capabilities, and qos_info to the TDLS peer */
	if (wpa_tdls_addset_peer(sm, peer, 0) < 0)
		goto error;

	wpa_printf(MSG_DEBUG, "TDLS: Sending TDLS Setup Confirm / "
		   "TPK Handshake Message 3");
	if (wpa_tdls_send_tpk_m3(sm, src_addr, dtoken, lnkid, peer) < 0)
		goto error_no_msg;

	if (!peer->tpk_success) {
		/*
		 * Enable Link only when tpk_success is 0, signifying that this
		 * processing of TPK M2 frame is not because of a retransmission
		 * during TDLS setup handshake.
		 */
		ret = wpa_tdls_enable_link(sm, peer);
		if (ret < 0) {
			wpa_printf(MSG_DEBUG, "TDLS: Could not enable link");
			wpa_tdls_do_teardown(
				sm, peer,
				WLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED);
		}
	}
	return ret;

error:
	wpa_tdls_send_error(sm, src_addr, WLAN_TDLS_SETUP_CONFIRM, dtoken, 1,
			    status);
error_no_msg:
	wpa_tdls_disable_peer_link(sm, peer);
	return -1;
}


static int wpa_tdls_process_tpk_m3(struct wpa_sm *sm, const u8 *src_addr,
				   const u8 *buf, size_t len)
{
	struct wpa_tdls_peer *peer;
	struct wpa_eapol_ie_parse kde;
	struct wpa_tdls_ftie *ftie;
	struct wpa_tdls_timeoutie *timeoutie;
	struct wpa_tdls_lnkid *lnkid;
	int ielen;
	u16 status;
	const u8 *pos;
	u32 lifetime;
	int ret = 0;

	wpa_printf(MSG_DEBUG, "TDLS: Received TDLS Setup Confirm / TPK M3 "
		   "(Peer " MACSTR ")", MAC2STR(src_addr));
	for (peer = sm->tdls; peer; peer = peer->next) {
		if (os_memcmp(peer->addr, src_addr, ETH_ALEN) == 0)
			break;
	}
	if (peer == NULL) {
		wpa_printf(MSG_INFO, "TDLS: No matching peer found for "
			   "TPK M3: " MACSTR, MAC2STR(src_addr));
		return -1;
	}
	wpa_tdls_tpk_retry_timeout_cancel(sm, peer, WLAN_TDLS_SETUP_RESPONSE);

	if (len < 3 + 3)
		goto error;
	pos = buf;
	pos += 1 /* pkt_type */ + 1 /* Category */ + 1 /* Action */;

	status = WPA_GET_LE16(pos);

	if (status != 0) {
		wpa_printf(MSG_INFO, "TDLS: Status code in TPK M3: %u",
			   status);
		goto error;
	}
	pos += 2 /* status code */ + 1 /* dialog token */;

	ielen = len - (pos - buf); /* start of IE in buf */

	/*
	 * Don't reject the message if failing to parse IEs. The IEs we need are
	 * explicitly checked below. Some APs piggy-back broken IEs to the end
	 * of a TDLS Confirm packet, which will fail the link if we don't ignore
	 * this error.
	 */
	if (wpa_supplicant_parse_ies((const u8 *) pos, ielen, &kde) < 0) {
		wpa_printf(MSG_DEBUG,
			   "TDLS: Failed to parse KDEs in TPK M3 - ignore as an interop workaround");
	}

	if (kde.lnkid == NULL || kde.lnkid_len < 3 * ETH_ALEN) {
		wpa_printf(MSG_INFO, "TDLS: No Link Identifier IE in TPK M3");
		goto error;
	}
	wpa_hexdump(MSG_DEBUG, "TDLS: Link ID Received from TPK M3",
		    (u8 *) kde.lnkid, kde.lnkid_len);
	lnkid = (struct wpa_tdls_lnkid *) kde.lnkid;

	if (os_memcmp(sm->bssid, lnkid->bssid, ETH_ALEN) != 0) {
		wpa_printf(MSG_INFO, "TDLS: TPK M3 from diff BSS");
		goto error;
	}

	if (!wpa_tdls_get_privacy(sm))
		goto skip_rsn;

	if (kde.ftie == NULL || kde.ftie_len < sizeof(*ftie)) {
		wpa_printf(MSG_INFO, "TDLS: No FTIE in TPK M3");
		goto error;
	}
	wpa_hexdump(MSG_DEBUG, "TDLS: FTIE Received from TPK M3",
		    kde.ftie, sizeof(*ftie));
	ftie = (struct wpa_tdls_ftie *) kde.ftie;

	if (kde.rsn_ie == NULL) {
		wpa_printf(MSG_INFO, "TDLS: No RSN IE in TPK M3");
		goto error;
	}
	wpa_hexdump(MSG_DEBUG, "TDLS: RSN IE Received from TPK M3",
		    kde.rsn_ie, kde.rsn_ie_len);
	if (kde.rsn_ie_len != peer->rsnie_p_len ||
	    os_memcmp(kde.rsn_ie, peer->rsnie_p, peer->rsnie_p_len) != 0) {
		wpa_printf(MSG_INFO, "TDLS: RSN IE in TPK M3 does not match "
			   "with the one sent in TPK M2");
		goto error;
	}

	if (os_memcmp(peer->rnonce, ftie->Anonce, WPA_NONCE_LEN) != 0) {
		wpa_printf(MSG_INFO, "TDLS: FTIE ANonce in TPK M3 does "
			   "not match with FTIE ANonce used in TPK M2");
		goto error;
	}

	if (os_memcmp(peer->inonce, ftie->Snonce, WPA_NONCE_LEN) != 0) {
		wpa_printf(MSG_INFO, "TDLS: FTIE SNonce in TPK M3 does not "
			   "match with FTIE SNonce used in TPK M1");
		goto error;
	}

	if (kde.key_lifetime == NULL) {
		wpa_printf(MSG_INFO, "TDLS: No Key Lifetime IE in TPK M3");
		goto error;
	}
	timeoutie = (struct wpa_tdls_timeoutie *) kde.key_lifetime;
	wpa_hexdump(MSG_DEBUG, "TDLS: Timeout IE Received from TPK M3",
		    (u8 *) timeoutie, sizeof(*timeoutie));
	lifetime = WPA_GET_LE32(timeoutie->value);
	wpa_printf(MSG_DEBUG, "TDLS: TPK lifetime %u seconds in TPK M3",
		   lifetime);
	if (lifetime != peer->lifetime) {
		wpa_printf(MSG_INFO, "TDLS: Unexpected TPK lifetime %u in "
			   "TPK M3 (expected %u)", lifetime, peer->lifetime);
		goto error;
	}

	if (wpa_supplicant_verify_tdls_mic(3, peer, (u8 *) lnkid,
					   (u8 *) timeoutie, ftie) < 0) {
		wpa_tdls_del_key(sm, peer);
		goto error;
	}

	if (wpa_tdls_set_key(sm, peer) < 0) {
		/*
		 * Some drivers may not be able to config the key prior to full
		 * STA entry having been configured.
		 */
		wpa_printf(MSG_DEBUG, "TDLS: Try to configure TPK again after "
			   "STA entry is complete");
		peer->reconfig_key = 1;
	}

skip_rsn:
	/* add supported rates, capabilities, and qos_info to the TDLS peer */
	if (wpa_tdls_addset_peer(sm, peer, 0) < 0)
		goto error;

	if (!peer->tpk_success) {
		/*
		 * Enable Link only when tpk_success is 0, signifying that this
		 * processing of TPK M3 frame is not because of a retransmission
		 * during TDLS setup handshake.
		 */
		ret = wpa_tdls_enable_link(sm, peer);
		if (ret < 0) {
			wpa_printf(MSG_DEBUG, "TDLS: Could not enable link");
			goto error;
		}
	}
	return ret;
error:
	wpa_tdls_do_teardown(sm, peer, WLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED);
	return -1;
}


static u8 * wpa_add_tdls_timeoutie(u8 *pos, u8 *ie, size_t ie_len, u32 tsecs)
{
	struct wpa_tdls_timeoutie *lifetime = (struct wpa_tdls_timeoutie *) ie;

	os_memset(lifetime, 0, ie_len);
	lifetime->ie_type = WLAN_EID_TIMEOUT_INTERVAL;
	lifetime->ie_len = sizeof(struct wpa_tdls_timeoutie) - 2;
	lifetime->interval_type = WLAN_TIMEOUT_KEY_LIFETIME;
	WPA_PUT_LE32(lifetime->value, tsecs);
	os_memcpy(pos, ie, ie_len);
	return pos + ie_len;
}


/**
 * wpa_tdls_start - Initiate TDLS handshake (send TPK Handshake Message 1)
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @peer: MAC address of the peer STA
 * Returns: 0 on success, or -1 on failure
 *
 * Send TPK Handshake Message 1 info to driver to start TDLS
 * handshake with the peer.
 */
int wpa_tdls_start(struct wpa_sm *sm, const u8 *addr)
{
	struct wpa_tdls_peer *peer;
	int tdls_prohibited = sm->tdls_prohibited;

	if (sm->tdls_disabled || !sm->tdls_supported)
		return -1;

#ifdef CONFIG_TDLS_TESTING
	if ((tdls_testing & TDLS_TESTING_IGNORE_AP_PROHIBIT) &&
	    tdls_prohibited) {
		wpa_printf(MSG_DEBUG, "TDLS: Testing - ignore AP prohibition "
			   "on TDLS");
		tdls_prohibited = 0;
	}
#endif /* CONFIG_TDLS_TESTING */

	if (tdls_prohibited) {
		wpa_printf(MSG_DEBUG, "TDLS: TDLS is prohibited in this BSS - "
			   "reject request to start setup");
		return -1;
	}

	peer = wpa_tdls_add_peer(sm, addr, NULL);
	if (peer == NULL)
		return -1;

	if (peer->tpk_in_progress) {
		wpa_printf(MSG_DEBUG, "TDLS: Setup is already in progress with the peer");
		return 0;
	}

	peer->initiator = 1;

	/* add the peer to the driver as a "setup in progress" peer */
	if (wpa_sm_tdls_peer_addset(sm, peer->addr, 1, 0, 0, NULL, 0, NULL,
				    NULL, 0, 0, NULL, 0, NULL, 0, NULL, 0)) {
		wpa_tdls_disable_peer_link(sm, peer);
		return -1;
	}

	peer->tpk_in_progress = 1;

	if (wpa_tdls_send_tpk_m1(sm, peer) < 0) {
		wpa_tdls_disable_peer_link(sm, peer);
		return -1;
	}

	return 0;
}


void wpa_tdls_remove(struct wpa_sm *sm, const u8 *addr)
{
	struct wpa_tdls_peer *peer;

	if (sm->tdls_disabled || !sm->tdls_supported)
		return;

	for (peer = sm->tdls; peer; peer = peer->next) {
		if (os_memcmp(peer->addr, addr, ETH_ALEN) == 0)
			break;
	}

	if (peer == NULL || !peer->tpk_success)
		return;

	if (sm->tdls_external_setup) {
		/*
		 * Disable previous link to allow renegotiation to be completed
		 * on AP path.
		 */
		wpa_tdls_do_teardown(sm, peer,
				     WLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED);
	}
}


/**
 * wpa_supplicant_rx_tdls - Receive TDLS data frame
 *
 * This function is called to receive TDLS (ethertype = 0x890d) data frames.
 */
static void wpa_supplicant_rx_tdls(void *ctx, const u8 *src_addr,
				   const u8 *buf, size_t len)
{
	struct wpa_sm *sm = ctx;
	struct wpa_tdls_frame *tf;

	wpa_hexdump(MSG_DEBUG, "TDLS: Received Data frame encapsulation",
		    buf, len);

	if (sm->tdls_disabled || !sm->tdls_supported) {
		wpa_printf(MSG_DEBUG, "TDLS: Discard message - TDLS disabled "
			   "or unsupported by driver");
		return;
	}

	if (os_memcmp(src_addr, sm->own_addr, ETH_ALEN) == 0) {
		wpa_printf(MSG_DEBUG, "TDLS: Discard copy of own message");
		return;
	}

	if (len < sizeof(*tf)) {
		wpa_printf(MSG_INFO, "TDLS: Drop too short frame");
		return;
	}

	/* Check to make sure its a valid encapsulated TDLS frame */
	tf = (struct wpa_tdls_frame *) buf;
	if (tf->payloadtype != 2 /* TDLS_RFTYPE */ ||
	    tf->category != WLAN_ACTION_TDLS) {
		wpa_printf(MSG_INFO, "TDLS: Invalid frame - payloadtype=%u "
			   "category=%u action=%u",
			   tf->payloadtype, tf->category, tf->action);
		return;
	}

	switch (tf->action) {
	case WLAN_TDLS_SETUP_REQUEST:
		wpa_tdls_process_tpk_m1(sm, src_addr, buf, len);
		break;
	case WLAN_TDLS_SETUP_RESPONSE:
		wpa_tdls_process_tpk_m2(sm, src_addr, buf, len);
		break;
	case WLAN_TDLS_SETUP_CONFIRM:
		wpa_tdls_process_tpk_m3(sm, src_addr, buf, len);
		break;
	case WLAN_TDLS_TEARDOWN:
		wpa_tdls_recv_teardown(sm, src_addr, buf, len);
		break;
	case WLAN_TDLS_DISCOVERY_REQUEST:
		wpa_tdls_process_discovery_request(sm, src_addr, buf, len);
		break;
	default:
		/* Kernel code will process remaining frames */
		wpa_printf(MSG_DEBUG, "TDLS: Ignore TDLS frame action code %u",
			   tf->action);
		break;
	}
}


/**
 * wpa_tdls_init - Initialize driver interface parameters for TDLS
 * @wpa_s: Pointer to wpa_supplicant data
 * Returns: 0 on success, -1 on failure
 *
 * This function is called to initialize driver interface parameters for TDLS.
 * wpa_drv_init() must have been called before this function to initialize the
 * driver interface.
 */
int wpa_tdls_init(struct wpa_sm *sm)
{
	if (sm == NULL)
		return -1;

	sm->l2_tdls = l2_packet_init(sm->bridge_ifname ? sm->bridge_ifname :
				     sm->ifname,
				     sm->own_addr,
				     ETH_P_80211_ENCAP, wpa_supplicant_rx_tdls,
				     sm, 0);
	if (sm->l2_tdls == NULL) {
		wpa_printf(MSG_ERROR, "TDLS: Failed to open l2_packet "
			   "connection");
		return -1;
	}

	/*
	 * Drivers that support TDLS but don't implement the get_capa callback
	 * are assumed to perform everything internally
	 */
	if (wpa_sm_tdls_get_capa(sm, &sm->tdls_supported,
				 &sm->tdls_external_setup,
				 &sm->tdls_chan_switch) < 0) {
		sm->tdls_supported = 1;
		sm->tdls_external_setup = 0;
	}

	wpa_printf(MSG_DEBUG, "TDLS: TDLS operation%s supported by "
		   "driver", sm->tdls_supported ? "" : " not");
	wpa_printf(MSG_DEBUG, "TDLS: Driver uses %s link setup",
		   sm->tdls_external_setup ? "external" : "internal");
	wpa_printf(MSG_DEBUG, "TDLS: Driver %s TDLS channel switching",
		   sm->tdls_chan_switch ? "supports" : "does not support");

	return 0;
}


void wpa_tdls_teardown_peers(struct wpa_sm *sm)
{
	struct wpa_tdls_peer *peer, *tmp;

	if (!sm)
		return;
	peer = sm->tdls;

	wpa_printf(MSG_DEBUG, "TDLS: Tear down peers");

	while (peer) {
		tmp = peer->next;
		wpa_printf(MSG_DEBUG, "TDLS: Tear down peer " MACSTR,
			   MAC2STR(peer->addr));
		if (sm->tdls_external_setup)
			wpa_tdls_do_teardown(sm, peer,
					     WLAN_REASON_DEAUTH_LEAVING);
		else
			wpa_sm_tdls_oper(sm, TDLS_TEARDOWN, peer->addr);

		peer = tmp;
	}
}


static void wpa_tdls_remove_peers(struct wpa_sm *sm)
{
	struct wpa_tdls_peer *peer, *tmp;

	peer = sm->tdls;

	while (peer) {
		int res;
		tmp = peer->next;
		res = wpa_sm_tdls_oper(sm, TDLS_DISABLE_LINK, peer->addr);
		wpa_printf(MSG_DEBUG, "TDLS: Remove peer " MACSTR " (res=%d)",
			   MAC2STR(peer->addr), res);
		wpa_tdls_peer_free(sm, peer);
		peer = tmp;
	}
}


/**
 * wpa_tdls_deinit - Deinitialize driver interface parameters for TDLS
 *
 * This function is called to recover driver interface parameters for TDLS
 * and frees resources allocated for it.
 */
void wpa_tdls_deinit(struct wpa_sm *sm)
{
	if (sm == NULL)
		return;

	if (sm->l2_tdls)
		l2_packet_deinit(sm->l2_tdls);
	sm->l2_tdls = NULL;

	wpa_tdls_remove_peers(sm);
}


void wpa_tdls_assoc(struct wpa_sm *sm)
{
	wpa_printf(MSG_DEBUG, "TDLS: Remove peers on association");
	wpa_tdls_remove_peers(sm);
}


void wpa_tdls_disassoc(struct wpa_sm *sm)
{
	wpa_printf(MSG_DEBUG, "TDLS: Remove peers on disassociation");
	wpa_tdls_remove_peers(sm);
}


static int wpa_tdls_prohibited(struct ieee802_11_elems *elems)
{
	/* bit 38 - TDLS Prohibited */
	return !!(elems->ext_capab[4] & 0x40);
}


static int wpa_tdls_chan_switch_prohibited(struct ieee802_11_elems *elems)
{
	/* bit 39 - TDLS Channel Switch Prohibited */
	return !!(elems->ext_capab[4] & 0x80);
}


void wpa_tdls_ap_ies(struct wpa_sm *sm, const u8 *ies, size_t len)
{
	struct ieee802_11_elems elems;

	sm->tdls_prohibited = 0;
	sm->tdls_chan_switch_prohibited = 0;

	if (ies == NULL ||
	    ieee802_11_parse_elems(ies, len, &elems, 0) == ParseFailed ||
	    elems.ext_capab == NULL || elems.ext_capab_len < 5)
		return;

	sm->tdls_prohibited = wpa_tdls_prohibited(&elems);
	wpa_printf(MSG_DEBUG, "TDLS: TDLS is %s in the target BSS",
		   sm->tdls_prohibited ? "prohibited" : "allowed");
	sm->tdls_chan_switch_prohibited =
		wpa_tdls_chan_switch_prohibited(&elems);
	wpa_printf(MSG_DEBUG, "TDLS: TDLS channel switch %s in the target BSS",
		   sm->tdls_chan_switch_prohibited ? "prohibited" : "allowed");
}


void wpa_tdls_assoc_resp_ies(struct wpa_sm *sm, const u8 *ies, size_t len)
{
	struct ieee802_11_elems elems;

	if (ies == NULL ||
	    ieee802_11_parse_elems(ies, len, &elems, 0) == ParseFailed ||
	    elems.ext_capab == NULL || elems.ext_capab_len < 5)
		return;

	if (!sm->tdls_prohibited && wpa_tdls_prohibited(&elems)) {
		wpa_printf(MSG_DEBUG, "TDLS: TDLS prohibited based on "
			   "(Re)Association Response IEs");
		sm->tdls_prohibited = 1;
	}

	if (!sm->tdls_chan_switch_prohibited &&
	    wpa_tdls_chan_switch_prohibited(&elems)) {
		wpa_printf(MSG_DEBUG,
			   "TDLS: TDLS channel switch prohibited based on (Re)Association Response IEs");
		sm->tdls_chan_switch_prohibited = 1;
	}
}


void wpa_tdls_enable(struct wpa_sm *sm, int enabled)
{
	wpa_printf(MSG_DEBUG, "TDLS: %s", enabled ? "enabled" : "disabled");
	sm->tdls_disabled = !enabled;
}


int wpa_tdls_is_external_setup(struct wpa_sm *sm)
{
	return sm->tdls_external_setup;
}


int wpa_tdls_enable_chan_switch(struct wpa_sm *sm, const u8 *addr,
				u8 oper_class,
				struct hostapd_freq_params *freq_params)
{
	struct wpa_tdls_peer *peer;
	int ret;

	if (sm->tdls_disabled || !sm->tdls_supported)
		return -1;

	if (!sm->tdls_chan_switch) {
		wpa_printf(MSG_DEBUG,
			   "TDLS: Channel switching not supported by the driver");
		return -1;
	}

	if (sm->tdls_chan_switch_prohibited) {
		wpa_printf(MSG_DEBUG,
			   "TDLS: Channel switching is prohibited in this BSS - reject request to switch channel");
		return -1;
	}

	for (peer = sm->tdls; peer; peer = peer->next) {
		if (os_memcmp(peer->addr, addr, ETH_ALEN) == 0)
			break;
	}

	if (peer == NULL || !peer->tpk_success) {
		wpa_printf(MSG_ERROR, "TDLS: Peer " MACSTR
			   " not found for channel switching", MAC2STR(addr));
		return -1;
	}

	if (peer->chan_switch_enabled) {
		wpa_printf(MSG_DEBUG, "TDLS: Peer " MACSTR
			   " already has channel switching enabled",
			   MAC2STR(addr));
		return 0;
	}

	ret = wpa_sm_tdls_enable_channel_switch(sm, peer->addr,
						oper_class, freq_params);
	if (!ret)
		peer->chan_switch_enabled = 1;

	return ret;
}


int wpa_tdls_disable_chan_switch(struct wpa_sm *sm, const u8 *addr)
{
	struct wpa_tdls_peer *peer;

	if (sm->tdls_disabled || !sm->tdls_supported)
		return -1;

	for (peer = sm->tdls; peer; peer = peer->next) {
		if (os_memcmp(peer->addr, addr, ETH_ALEN) == 0)
			break;
	}

	if (!peer || !peer->chan_switch_enabled) {
		wpa_printf(MSG_ERROR, "TDLS: Channel switching not enabled for "
			   MACSTR, MAC2STR(addr));
		return -1;
	}

	/* ignore the return value */
	wpa_sm_tdls_disable_channel_switch(sm, peer->addr);

	peer->chan_switch_enabled = 0;
	return 0;
}
