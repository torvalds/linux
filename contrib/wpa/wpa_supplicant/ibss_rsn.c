/*
 * wpa_supplicant - IBSS RSN
 * Copyright (c) 2009-2013, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "common/wpa_ctrl.h"
#include "utils/eloop.h"
#include "l2_packet/l2_packet.h"
#include "rsn_supp/wpa.h"
#include "rsn_supp/wpa_ie.h"
#include "ap/wpa_auth.h"
#include "wpa_supplicant_i.h"
#include "driver_i.h"
#include "common/ieee802_11_defs.h"
#include "ibss_rsn.h"


static void ibss_rsn_auth_timeout(void *eloop_ctx, void *timeout_ctx);


static struct ibss_rsn_peer * ibss_rsn_get_peer(struct ibss_rsn *ibss_rsn,
						const u8 *addr)
{
	struct ibss_rsn_peer *peer;

	for (peer = ibss_rsn->peers; peer; peer = peer->next)
		if (os_memcmp(addr, peer->addr, ETH_ALEN) == 0)
			break;
	return peer;
}


static void ibss_rsn_free(struct ibss_rsn_peer *peer)
{
	eloop_cancel_timeout(ibss_rsn_auth_timeout, peer, NULL);
	wpa_auth_sta_deinit(peer->auth);
	wpa_sm_deinit(peer->supp);
	os_free(peer);
}


static void supp_set_state(void *ctx, enum wpa_states state)
{
	struct ibss_rsn_peer *peer = ctx;
	peer->supp_state = state;
}


static enum wpa_states supp_get_state(void *ctx)
{
	struct ibss_rsn_peer *peer = ctx;
	return peer->supp_state;
}


static int supp_ether_send(void *ctx, const u8 *dest, u16 proto, const u8 *buf,
			   size_t len)
{
	struct ibss_rsn_peer *peer = ctx;
	struct wpa_supplicant *wpa_s = peer->ibss_rsn->wpa_s;

	wpa_printf(MSG_DEBUG, "SUPP: %s(dest=" MACSTR " proto=0x%04x "
		   "len=%lu)",
		   __func__, MAC2STR(dest), proto, (unsigned long) len);

	if (wpa_s->l2)
		return l2_packet_send(wpa_s->l2, dest, proto, buf, len);

	return -1;
}


static u8 * supp_alloc_eapol(void *ctx, u8 type, const void *data,
			     u16 data_len, size_t *msg_len, void **data_pos)
{
	struct ieee802_1x_hdr *hdr;

	wpa_printf(MSG_DEBUG, "SUPP: %s(type=%d data_len=%d)",
		   __func__, type, data_len);

	*msg_len = sizeof(*hdr) + data_len;
	hdr = os_malloc(*msg_len);
	if (hdr == NULL)
		return NULL;

	hdr->version = 2;
	hdr->type = type;
	hdr->length = host_to_be16(data_len);

	if (data)
		os_memcpy(hdr + 1, data, data_len);
	else
		os_memset(hdr + 1, 0, data_len);

	if (data_pos)
		*data_pos = hdr + 1;

	return (u8 *) hdr;
}


static int supp_get_beacon_ie(void *ctx)
{
	struct ibss_rsn_peer *peer = ctx;

	wpa_printf(MSG_DEBUG, "SUPP: %s", __func__);
	/* TODO: get correct RSN IE */
	return wpa_sm_set_ap_rsn_ie(peer->supp,
				    (u8 *) "\x30\x14\x01\x00"
				    "\x00\x0f\xac\x04"
				    "\x01\x00\x00\x0f\xac\x04"
				    "\x01\x00\x00\x0f\xac\x02"
				    "\x00\x00", 22);
}


static void ibss_check_rsn_completed(struct ibss_rsn_peer *peer)
{
	struct wpa_supplicant *wpa_s = peer->ibss_rsn->wpa_s;

	if ((peer->authentication_status &
	     (IBSS_RSN_SET_PTK_SUPP | IBSS_RSN_SET_PTK_AUTH)) !=
	    (IBSS_RSN_SET_PTK_SUPP | IBSS_RSN_SET_PTK_AUTH))
		return;
	if (peer->authentication_status & IBSS_RSN_REPORTED_PTK)
		return;
	peer->authentication_status |= IBSS_RSN_REPORTED_PTK;
	wpa_msg(wpa_s, MSG_INFO, IBSS_RSN_COMPLETED MACSTR,
		MAC2STR(peer->addr));
}


static int supp_set_key(void *ctx, enum wpa_alg alg,
			const u8 *addr, int key_idx, int set_tx,
			const u8 *seq, size_t seq_len,
			const u8 *key, size_t key_len)
{
	struct ibss_rsn_peer *peer = ctx;

	wpa_printf(MSG_DEBUG, "SUPP: %s(alg=%d addr=" MACSTR " key_idx=%d "
		   "set_tx=%d)",
		   __func__, alg, MAC2STR(addr), key_idx, set_tx);
	wpa_hexdump(MSG_DEBUG, "SUPP: set_key - seq", seq, seq_len);
	wpa_hexdump_key(MSG_DEBUG, "SUPP: set_key - key", key, key_len);

	if (key_idx == 0) {
		peer->authentication_status |= IBSS_RSN_SET_PTK_SUPP;
		ibss_check_rsn_completed(peer);
		/*
		 * In IBSS RSN, the pairwise key from the 4-way handshake
		 * initiated by the peer with highest MAC address is used.
		 */
		if (os_memcmp(peer->ibss_rsn->wpa_s->own_addr, peer->addr,
			      ETH_ALEN) > 0) {
			wpa_printf(MSG_DEBUG, "SUPP: Do not use this PTK");
			return 0;
		}
	}

	if (is_broadcast_ether_addr(addr))
		addr = peer->addr;
	return wpa_drv_set_key(peer->ibss_rsn->wpa_s, alg, addr, key_idx,
			       set_tx, seq, seq_len, key, key_len);
}


static void * supp_get_network_ctx(void *ctx)
{
	struct ibss_rsn_peer *peer = ctx;
	return wpa_supplicant_get_ssid(peer->ibss_rsn->wpa_s);
}


static int supp_mlme_setprotection(void *ctx, const u8 *addr,
				   int protection_type, int key_type)
{
	wpa_printf(MSG_DEBUG, "SUPP: %s(addr=" MACSTR " protection_type=%d "
		   "key_type=%d)",
		   __func__, MAC2STR(addr), protection_type, key_type);
	return 0;
}


static void supp_cancel_auth_timeout(void *ctx)
{
	wpa_printf(MSG_DEBUG, "SUPP: %s", __func__);
}


static void supp_deauthenticate(void * ctx, int reason_code)
{
	wpa_printf(MSG_DEBUG, "SUPP: %s (TODO)", __func__);
}


static int ibss_rsn_supp_init(struct ibss_rsn_peer *peer, const u8 *own_addr,
			      const u8 *psk)
{
	struct wpa_sm_ctx *ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL)
		return -1;

	ctx->ctx = peer;
	ctx->msg_ctx = peer->ibss_rsn->wpa_s;
	ctx->set_state = supp_set_state;
	ctx->get_state = supp_get_state;
	ctx->ether_send = supp_ether_send;
	ctx->get_beacon_ie = supp_get_beacon_ie;
	ctx->alloc_eapol = supp_alloc_eapol;
	ctx->set_key = supp_set_key;
	ctx->get_network_ctx = supp_get_network_ctx;
	ctx->mlme_setprotection = supp_mlme_setprotection;
	ctx->cancel_auth_timeout = supp_cancel_auth_timeout;
	ctx->deauthenticate = supp_deauthenticate;
	peer->supp = wpa_sm_init(ctx);
	if (peer->supp == NULL) {
		wpa_printf(MSG_DEBUG, "SUPP: wpa_sm_init() failed");
		os_free(ctx);
		return -1;
	}

	wpa_sm_set_own_addr(peer->supp, own_addr);
	wpa_sm_set_param(peer->supp, WPA_PARAM_RSN_ENABLED, 1);
	wpa_sm_set_param(peer->supp, WPA_PARAM_PROTO, WPA_PROTO_RSN);
	wpa_sm_set_param(peer->supp, WPA_PARAM_PAIRWISE, WPA_CIPHER_CCMP);
	wpa_sm_set_param(peer->supp, WPA_PARAM_GROUP, WPA_CIPHER_CCMP);
	wpa_sm_set_param(peer->supp, WPA_PARAM_KEY_MGMT, WPA_KEY_MGMT_PSK);
	wpa_sm_set_pmk(peer->supp, psk, PMK_LEN, NULL, NULL);

	peer->supp_ie_len = sizeof(peer->supp_ie);
	if (wpa_sm_set_assoc_wpa_ie_default(peer->supp, peer->supp_ie,
					    &peer->supp_ie_len) < 0) {
		wpa_printf(MSG_DEBUG, "SUPP: wpa_sm_set_assoc_wpa_ie_default()"
			   " failed");
		return -1;
	}

	wpa_sm_notify_assoc(peer->supp, peer->addr);

	return 0;
}


static void auth_logger(void *ctx, const u8 *addr, logger_level level,
			const char *txt)
{
	if (addr)
		wpa_printf(MSG_DEBUG, "AUTH: " MACSTR " - %s",
			   MAC2STR(addr), txt);
	else
		wpa_printf(MSG_DEBUG, "AUTH: %s", txt);
}


static const u8 * auth_get_psk(void *ctx, const u8 *addr,
			       const u8 *p2p_dev_addr, const u8 *prev_psk,
			       size_t *psk_len)
{
	struct ibss_rsn *ibss_rsn = ctx;

	if (psk_len)
		*psk_len = PMK_LEN;
	wpa_printf(MSG_DEBUG, "AUTH: %s (addr=" MACSTR " prev_psk=%p)",
		   __func__, MAC2STR(addr), prev_psk);
	if (prev_psk)
		return NULL;
	return ibss_rsn->psk;
}


static int auth_send_eapol(void *ctx, const u8 *addr, const u8 *data,
			   size_t data_len, int encrypt)
{
	struct ibss_rsn *ibss_rsn = ctx;
	struct wpa_supplicant *wpa_s = ibss_rsn->wpa_s;

	wpa_printf(MSG_DEBUG, "AUTH: %s(addr=" MACSTR " data_len=%lu "
		   "encrypt=%d)",
		   __func__, MAC2STR(addr), (unsigned long) data_len, encrypt);

	if (wpa_s->l2)
		return l2_packet_send(wpa_s->l2, addr, ETH_P_EAPOL, data,
				      data_len);

	return -1;
}


static int auth_set_key(void *ctx, int vlan_id, enum wpa_alg alg,
			const u8 *addr, int idx, u8 *key, size_t key_len)
{
	struct ibss_rsn *ibss_rsn = ctx;
	u8 seq[6];

	os_memset(seq, 0, sizeof(seq));

	if (addr) {
		wpa_printf(MSG_DEBUG, "AUTH: %s(alg=%d addr=" MACSTR
			   " key_idx=%d)",
			   __func__, alg, MAC2STR(addr), idx);
	} else {
		wpa_printf(MSG_DEBUG, "AUTH: %s(alg=%d key_idx=%d)",
			   __func__, alg, idx);
	}
	wpa_hexdump_key(MSG_DEBUG, "AUTH: set_key - key", key, key_len);

	if (idx == 0) {
		if (addr) {
			struct ibss_rsn_peer *peer;
			peer = ibss_rsn_get_peer(ibss_rsn, addr);
			if (peer) {
				peer->authentication_status |=
					IBSS_RSN_SET_PTK_AUTH;
				ibss_check_rsn_completed(peer);
			}
		}
		/*
		 * In IBSS RSN, the pairwise key from the 4-way handshake
		 * initiated by the peer with highest MAC address is used.
		 */
		if (addr == NULL ||
		    os_memcmp(ibss_rsn->wpa_s->own_addr, addr, ETH_ALEN) < 0) {
			wpa_printf(MSG_DEBUG, "AUTH: Do not use this PTK");
			return 0;
		}
	}

	return wpa_drv_set_key(ibss_rsn->wpa_s, alg, addr, idx,
			       1, seq, 6, key, key_len);
}


static void ibss_rsn_disconnect(void *ctx, const u8 *addr, u16 reason)
{
	struct ibss_rsn *ibss_rsn = ctx;
	wpa_drv_sta_deauth(ibss_rsn->wpa_s, addr, reason);
}


static int auth_for_each_sta(void *ctx, int (*cb)(struct wpa_state_machine *sm,
						  void *ctx),
			     void *cb_ctx)
{
	struct ibss_rsn *ibss_rsn = ctx;
	struct ibss_rsn_peer *peer;

	wpa_printf(MSG_DEBUG, "AUTH: for_each_sta");

	for (peer = ibss_rsn->peers; peer; peer = peer->next) {
		if (peer->auth && cb(peer->auth, cb_ctx))
			return 1;
	}

	return 0;
}


static void ibss_set_sta_authorized(struct ibss_rsn *ibss_rsn,
				    struct ibss_rsn_peer *peer, int authorized)
{
	int res;

	if (authorized) {
		res = wpa_drv_sta_set_flags(ibss_rsn->wpa_s, peer->addr,
					    WPA_STA_AUTHORIZED,
					    WPA_STA_AUTHORIZED, ~0);
		wpa_printf(MSG_DEBUG, "AUTH: " MACSTR " authorizing port",
			   MAC2STR(peer->addr));
	} else {
		res = wpa_drv_sta_set_flags(ibss_rsn->wpa_s, peer->addr,
					    0, 0, ~WPA_STA_AUTHORIZED);
		wpa_printf(MSG_DEBUG, "AUTH: " MACSTR " unauthorizing port",
			   MAC2STR(peer->addr));
	}

	if (res && errno != ENOENT) {
		wpa_printf(MSG_DEBUG, "Could not set station " MACSTR " flags "
			   "for kernel driver (errno=%d)",
			   MAC2STR(peer->addr), errno);
	}
}


static void auth_set_eapol(void *ctx, const u8 *addr,
				       wpa_eapol_variable var, int value)
{
	struct ibss_rsn *ibss_rsn = ctx;
	struct ibss_rsn_peer *peer = ibss_rsn_get_peer(ibss_rsn, addr);

	if (peer == NULL)
		return;

	switch (var) {
	case WPA_EAPOL_authorized:
		ibss_set_sta_authorized(ibss_rsn, peer, value);
		break;
	default:
		/* do not handle any other event */
		wpa_printf(MSG_DEBUG, "AUTH: eapol event not handled %d", var);
		break;
	}
}


static int ibss_rsn_auth_init_group(struct ibss_rsn *ibss_rsn,
				    const u8 *own_addr, struct wpa_ssid *ssid)
{
	struct wpa_auth_config conf;
	static const struct wpa_auth_callbacks cb = {
		.logger = auth_logger,
		.set_eapol = auth_set_eapol,
		.send_eapol = auth_send_eapol,
		.get_psk = auth_get_psk,
		.set_key = auth_set_key,
		.for_each_sta = auth_for_each_sta,
		.disconnect = ibss_rsn_disconnect,
	};

	wpa_printf(MSG_DEBUG, "AUTH: Initializing group state machine");

	os_memset(&conf, 0, sizeof(conf));
	conf.wpa = 2;
	conf.wpa_key_mgmt = WPA_KEY_MGMT_PSK;
	conf.wpa_pairwise = WPA_CIPHER_CCMP;
	conf.rsn_pairwise = WPA_CIPHER_CCMP;
	conf.wpa_group = WPA_CIPHER_CCMP;
	conf.eapol_version = 2;
	conf.wpa_group_rekey = ssid->group_rekey ? ssid->group_rekey : 600;
	conf.wpa_group_update_count = 4;
	conf.wpa_pairwise_update_count = 4;

	ibss_rsn->auth_group = wpa_init(own_addr, &conf, &cb, ibss_rsn);
	if (ibss_rsn->auth_group == NULL) {
		wpa_printf(MSG_DEBUG, "AUTH: wpa_init() failed");
		return -1;
	}

	wpa_init_keys(ibss_rsn->auth_group);

	return 0;
}


static int ibss_rsn_auth_init(struct ibss_rsn *ibss_rsn,
			      struct ibss_rsn_peer *peer)
{
	peer->auth = wpa_auth_sta_init(ibss_rsn->auth_group, peer->addr, NULL);
	if (peer->auth == NULL) {
		wpa_printf(MSG_DEBUG, "AUTH: wpa_auth_sta_init() failed");
		return -1;
	}

	/* TODO: get peer RSN IE with Probe Request */
	if (wpa_validate_wpa_ie(ibss_rsn->auth_group, peer->auth,
				(u8 *) "\x30\x14\x01\x00"
				"\x00\x0f\xac\x04"
				"\x01\x00\x00\x0f\xac\x04"
				"\x01\x00\x00\x0f\xac\x02"
				"\x00\x00", 22, NULL, 0, NULL, 0) !=
	    WPA_IE_OK) {
		wpa_printf(MSG_DEBUG, "AUTH: wpa_validate_wpa_ie() failed");
		return -1;
	}

	if (wpa_auth_sm_event(peer->auth, WPA_ASSOC))
		return -1;

	if (wpa_auth_sta_associated(ibss_rsn->auth_group, peer->auth))
		return -1;

	return 0;
}


static int ibss_rsn_send_auth(struct ibss_rsn *ibss_rsn, const u8 *da, int seq)
{
	struct ieee80211_mgmt auth;
	const size_t auth_length = IEEE80211_HDRLEN + sizeof(auth.u.auth);
	struct wpa_supplicant *wpa_s = ibss_rsn->wpa_s;

	if (wpa_s->driver->send_frame == NULL)
		return -1;

	os_memset(&auth, 0, sizeof(auth));

	auth.frame_control = IEEE80211_FC(WLAN_FC_TYPE_MGMT,
					  WLAN_FC_STYPE_AUTH);
	os_memcpy(auth.da, da, ETH_ALEN);
	os_memcpy(auth.sa, wpa_s->own_addr, ETH_ALEN);
	os_memcpy(auth.bssid, wpa_s->bssid, ETH_ALEN);

	auth.u.auth.auth_alg = host_to_le16(WLAN_AUTH_OPEN);
	auth.u.auth.auth_transaction = host_to_le16(seq);
	auth.u.auth.status_code = host_to_le16(WLAN_STATUS_SUCCESS);

	wpa_printf(MSG_DEBUG, "RSN: IBSS TX Auth frame (SEQ %d) to " MACSTR,
		   seq, MAC2STR(da));

	return wpa_s->driver->send_frame(wpa_s->drv_priv, (u8 *) &auth,
					 auth_length, 0);
}


static int ibss_rsn_is_auth_started(struct ibss_rsn_peer * peer)
{
	return peer->authentication_status &
	       (IBSS_RSN_AUTH_BY_US | IBSS_RSN_AUTH_EAPOL_BY_US);
}


static struct ibss_rsn_peer *
ibss_rsn_peer_init(struct ibss_rsn *ibss_rsn, const u8 *addr)
{
	struct ibss_rsn_peer *peer;
	if (ibss_rsn == NULL)
		return NULL;

	peer = ibss_rsn_get_peer(ibss_rsn, addr);
	if (peer) {
		wpa_printf(MSG_DEBUG, "RSN: IBSS Supplicant for peer "MACSTR
			   " already running", MAC2STR(addr));
		return peer;
	}

	wpa_printf(MSG_DEBUG, "RSN: Starting IBSS Supplicant for peer "MACSTR,
		   MAC2STR(addr));

	peer = os_zalloc(sizeof(*peer));
	if (peer == NULL) {
		wpa_printf(MSG_DEBUG, "RSN: Could not allocate memory.");
		return NULL;
	}

	peer->ibss_rsn = ibss_rsn;
	os_memcpy(peer->addr, addr, ETH_ALEN);
	peer->authentication_status = IBSS_RSN_AUTH_NOT_AUTHENTICATED;

	if (ibss_rsn_supp_init(peer, ibss_rsn->wpa_s->own_addr,
			       ibss_rsn->psk) < 0) {
		ibss_rsn_free(peer);
		return NULL;
	}

	peer->next = ibss_rsn->peers;
	ibss_rsn->peers = peer;

	return peer;
}


static void ibss_rsn_auth_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct ibss_rsn_peer *peer = eloop_ctx;

	/*
	 * Assume peer does not support Authentication exchange or the frame was
	 * lost somewhere - start EAPOL Authenticator.
	 */
	wpa_printf(MSG_DEBUG,
		   "RSN: Timeout on waiting Authentication frame response from "
		   MACSTR " - start authenticator", MAC2STR(peer->addr));

	peer->authentication_status |= IBSS_RSN_AUTH_BY_US;
	ibss_rsn_auth_init(peer->ibss_rsn, peer);
}


int ibss_rsn_start(struct ibss_rsn *ibss_rsn, const u8 *addr)
{
	struct ibss_rsn_peer *peer;
	int res;

	if (!ibss_rsn)
		return -1;

	/* if the peer already exists, exit immediately */
	peer = ibss_rsn_get_peer(ibss_rsn, addr);
	if (peer)
		return 0;

	peer = ibss_rsn_peer_init(ibss_rsn, addr);
	if (peer == NULL)
		return -1;

	/* Open Authentication: send first Authentication frame */
	res = ibss_rsn_send_auth(ibss_rsn, addr, 1);
	if (res) {
		/*
		 * The driver may not support Authentication frame exchange in
		 * IBSS. Ignore authentication and go through EAPOL exchange.
		 */
		peer->authentication_status |= IBSS_RSN_AUTH_BY_US;
		return ibss_rsn_auth_init(ibss_rsn, peer);
	} else {
		os_get_reltime(&peer->own_auth_tx);
		eloop_register_timeout(1, 0, ibss_rsn_auth_timeout, peer, NULL);
	}

	return 0;
}


static int ibss_rsn_peer_authenticated(struct ibss_rsn *ibss_rsn,
				       struct ibss_rsn_peer *peer, int reason)
{
	int already_started;

	if (ibss_rsn == NULL || peer == NULL)
		return -1;

	already_started = ibss_rsn_is_auth_started(peer);
	peer->authentication_status |= reason;

	if (already_started) {
		wpa_printf(MSG_DEBUG, "RSN: IBSS Authenticator already "
			   "started for peer " MACSTR, MAC2STR(peer->addr));
		return 0;
	}

	wpa_printf(MSG_DEBUG, "RSN: Starting IBSS Authenticator "
		   "for now-authenticated peer " MACSTR, MAC2STR(peer->addr));

	return ibss_rsn_auth_init(ibss_rsn, peer);
}


void ibss_rsn_stop(struct ibss_rsn *ibss_rsn, const u8 *peermac)
{
	struct ibss_rsn_peer *peer, *prev;

	if (ibss_rsn == NULL)
		return;

	if (peermac == NULL) {
		/* remove all peers */
		wpa_printf(MSG_DEBUG, "%s: Remove all peers", __func__);
		peer = ibss_rsn->peers;
		while (peer) {
			prev = peer;
			peer = peer->next;
			ibss_rsn_free(prev);
			ibss_rsn->peers = peer;
		}
	} else {
		/* remove specific peer */
		wpa_printf(MSG_DEBUG, "%s: Remove specific peer " MACSTR,
			   __func__, MAC2STR(peermac));

		for (prev = NULL, peer = ibss_rsn->peers; peer != NULL;
		     prev = peer, peer = peer->next) {
			if (os_memcmp(peermac, peer->addr, ETH_ALEN) == 0) {
				if (prev == NULL)
					ibss_rsn->peers = peer->next;
				else
					prev->next = peer->next;
				ibss_rsn_free(peer);
				wpa_printf(MSG_DEBUG, "%s: Successfully "
					   "removed a specific peer",
					   __func__);
				break;
			}
		}
	}
}


struct ibss_rsn * ibss_rsn_init(struct wpa_supplicant *wpa_s,
				struct wpa_ssid *ssid)
{
	struct ibss_rsn *ibss_rsn;

	ibss_rsn = os_zalloc(sizeof(*ibss_rsn));
	if (ibss_rsn == NULL)
		return NULL;
	ibss_rsn->wpa_s = wpa_s;

	if (ibss_rsn_auth_init_group(ibss_rsn, wpa_s->own_addr, ssid) < 0) {
		ibss_rsn_deinit(ibss_rsn);
		return NULL;
	}

	return ibss_rsn;
}


void ibss_rsn_deinit(struct ibss_rsn *ibss_rsn)
{
	struct ibss_rsn_peer *peer, *prev;

	if (ibss_rsn == NULL)
		return;

	peer = ibss_rsn->peers;
	while (peer) {
		prev = peer;
		peer = peer->next;
		ibss_rsn_free(prev);
	}

	if (ibss_rsn->auth_group)
		wpa_deinit(ibss_rsn->auth_group);
	os_free(ibss_rsn);

}


static int ibss_rsn_eapol_dst_supp(const u8 *buf, size_t len)
{
	const struct ieee802_1x_hdr *hdr;
	const struct wpa_eapol_key *key;
	u16 key_info;
	size_t plen;

	/* TODO: Support other EAPOL packets than just EAPOL-Key */

	if (len < sizeof(*hdr) + sizeof(*key))
		return -1;

	hdr = (const struct ieee802_1x_hdr *) buf;
	key = (const struct wpa_eapol_key *) (hdr + 1);
	plen = be_to_host16(hdr->length);

	if (hdr->version < EAPOL_VERSION) {
		/* TODO: backwards compatibility */
	}
	if (hdr->type != IEEE802_1X_TYPE_EAPOL_KEY) {
		wpa_printf(MSG_DEBUG, "RSN: EAPOL frame (type %u) discarded, "
			"not a Key frame", hdr->type);
		return -1;
	}
	if (plen > len - sizeof(*hdr) || plen < sizeof(*key)) {
		wpa_printf(MSG_DEBUG, "RSN: EAPOL frame payload size %lu "
			   "invalid (frame size %lu)",
			   (unsigned long) plen, (unsigned long) len);
		return -1;
	}

	if (key->type != EAPOL_KEY_TYPE_RSN) {
		wpa_printf(MSG_DEBUG, "RSN: EAPOL-Key type (%d) unknown, "
			   "discarded", key->type);
		return -1;
	}

	key_info = WPA_GET_BE16(key->key_info);

	return !!(key_info & WPA_KEY_INFO_ACK);
}


static int ibss_rsn_process_rx_eapol(struct ibss_rsn *ibss_rsn,
				     struct ibss_rsn_peer *peer,
				     const u8 *buf, size_t len)
{
	int supp;
	u8 *tmp;

	supp = ibss_rsn_eapol_dst_supp(buf, len);
	if (supp < 0)
		return -1;

	tmp = os_memdup(buf, len);
	if (tmp == NULL)
		return -1;
	if (supp) {
		peer->authentication_status |= IBSS_RSN_AUTH_EAPOL_BY_PEER;
		wpa_printf(MSG_DEBUG, "RSN: IBSS RX EAPOL for Supplicant from "
			   MACSTR, MAC2STR(peer->addr));
		wpa_sm_rx_eapol(peer->supp, peer->addr, tmp, len);
	} else {
		if (ibss_rsn_is_auth_started(peer) == 0) {
			wpa_printf(MSG_DEBUG, "RSN: IBSS EAPOL for "
				   "Authenticator dropped as " MACSTR " is not "
				   "authenticated", MAC2STR(peer->addr));
			os_free(tmp);
			return -1;
		}

		wpa_printf(MSG_DEBUG, "RSN: IBSS RX EAPOL for Authenticator "
			   "from "MACSTR, MAC2STR(peer->addr));
		wpa_receive(ibss_rsn->auth_group, peer->auth, tmp, len);
	}
	os_free(tmp);

	return 1;
}


int ibss_rsn_rx_eapol(struct ibss_rsn *ibss_rsn, const u8 *src_addr,
		      const u8 *buf, size_t len)
{
	struct ibss_rsn_peer *peer;

	if (ibss_rsn == NULL)
		return -1;

	peer = ibss_rsn_get_peer(ibss_rsn, src_addr);
	if (peer)
		return ibss_rsn_process_rx_eapol(ibss_rsn, peer, buf, len);

	if (ibss_rsn_eapol_dst_supp(buf, len) > 0) {
		/*
		 * Create new IBSS peer based on an EAPOL message from the peer
		 * Authenticator.
		 */
		peer = ibss_rsn_peer_init(ibss_rsn, src_addr);
		if (peer == NULL)
			return -1;

		/* assume the peer is authenticated already */
		wpa_printf(MSG_DEBUG, "RSN: IBSS Not using IBSS Auth for peer "
			   MACSTR, MAC2STR(src_addr));
		ibss_rsn_peer_authenticated(ibss_rsn, peer,
					    IBSS_RSN_AUTH_EAPOL_BY_US);

		return ibss_rsn_process_rx_eapol(ibss_rsn, ibss_rsn->peers,
						 buf, len);
	}

	return 0;
}

void ibss_rsn_set_psk(struct ibss_rsn *ibss_rsn, const u8 *psk)
{
	if (ibss_rsn == NULL)
		return;
	os_memcpy(ibss_rsn->psk, psk, PMK_LEN);
}


static void ibss_rsn_handle_auth_1_of_2(struct ibss_rsn *ibss_rsn,
					struct ibss_rsn_peer *peer,
					const u8* addr)
{
	wpa_printf(MSG_DEBUG, "RSN: IBSS RX Auth frame (SEQ 1) from " MACSTR,
		   MAC2STR(addr));

	if (peer &&
	    peer->authentication_status & (IBSS_RSN_SET_PTK_SUPP |
					   IBSS_RSN_SET_PTK_AUTH)) {
		/* Clear the TK for this pair to allow recovery from the case
		 * where the peer STA has restarted and lost its key while we
		 * still have a pairwise key configured. */
		wpa_printf(MSG_DEBUG, "RSN: Clear pairwise key for peer "
			   MACSTR, MAC2STR(addr));
		wpa_drv_set_key(ibss_rsn->wpa_s, WPA_ALG_NONE, addr, 0, 0,
				NULL, 0, NULL, 0);
	}

	if (peer &&
	    peer->authentication_status & IBSS_RSN_AUTH_EAPOL_BY_PEER) {
		if (peer->own_auth_tx.sec) {
			struct os_reltime now, diff;
			os_get_reltime(&now);
			os_reltime_sub(&now, &peer->own_auth_tx, &diff);
			if (diff.sec == 0 && diff.usec < 500000) {
				wpa_printf(MSG_DEBUG, "RSN: Skip IBSS reinit since only %u usec from own Auth frame TX",
					   (int) diff.usec);
				goto skip_reinit;
			}
		}
		/*
		 * A peer sent us an Authentication frame even though it already
		 * started an EAPOL session. We should reinit state machines
		 * here, but it's much more complicated than just deleting and
		 * recreating the state machine
		 */
		wpa_printf(MSG_DEBUG, "RSN: IBSS Reinitializing station "
			   MACSTR, MAC2STR(addr));

		ibss_rsn_stop(ibss_rsn, addr);
		peer = NULL;
	}

	if (!peer) {
		peer = ibss_rsn_peer_init(ibss_rsn, addr);
		if (!peer)
			return;

		wpa_printf(MSG_DEBUG, "RSN: IBSS Auth started by peer " MACSTR,
			   MAC2STR(addr));
	}

skip_reinit:
	/* reply with an Authentication frame now, before sending an EAPOL */
	ibss_rsn_send_auth(ibss_rsn, addr, 2);
	/* no need to start another AUTH challenge in the other way.. */
	ibss_rsn_peer_authenticated(ibss_rsn, peer, IBSS_RSN_AUTH_EAPOL_BY_US);
}


void ibss_rsn_handle_auth(struct ibss_rsn *ibss_rsn, const u8 *auth_frame,
			  size_t len)
{
	const struct ieee80211_mgmt *header;
	struct ibss_rsn_peer *peer;
	size_t auth_length;

	header = (const struct ieee80211_mgmt *) auth_frame;
	auth_length = IEEE80211_HDRLEN + sizeof(header->u.auth);

	if (ibss_rsn == NULL || len < auth_length)
		return;

	if (le_to_host16(header->u.auth.auth_alg) != WLAN_AUTH_OPEN ||
	    le_to_host16(header->u.auth.status_code) != WLAN_STATUS_SUCCESS)
		return;

	peer = ibss_rsn_get_peer(ibss_rsn, header->sa);

	switch (le_to_host16(header->u.auth.auth_transaction)) {
	case 1:
		ibss_rsn_handle_auth_1_of_2(ibss_rsn, peer, header->sa);
		break;
	case 2:
		wpa_printf(MSG_DEBUG, "RSN: IBSS RX Auth frame (SEQ 2) from "
			   MACSTR, MAC2STR(header->sa));
		if (!peer) {
			wpa_printf(MSG_DEBUG, "RSN: Received Auth seq 2 from "
				   "unknown STA " MACSTR, MAC2STR(header->sa));
			break;
		}

		/* authentication has been completed */
		eloop_cancel_timeout(ibss_rsn_auth_timeout, peer, NULL);
		wpa_printf(MSG_DEBUG, "RSN: IBSS Auth completed with " MACSTR,
			   MAC2STR(header->sa));
		ibss_rsn_peer_authenticated(ibss_rsn, peer,
					    IBSS_RSN_AUTH_BY_US);
		break;
	}
}
