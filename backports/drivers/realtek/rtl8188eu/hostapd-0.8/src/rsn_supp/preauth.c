/*
 * RSN pre-authentication (supplicant)
 * Copyright (c) 2003-2010, Jouni Malinen <j@w1.fi>
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

#include "common.h"
#include "wpa.h"
#include "eloop.h"
#include "l2_packet/l2_packet.h"
#include "eapol_supp/eapol_supp_sm.h"
#include "preauth.h"
#include "pmksa_cache.h"
#include "wpa_i.h"
#include "common/ieee802_11_defs.h"


#if defined(IEEE8021X_EAPOL) && !defined(CONFIG_NO_WPA2)

#define PMKID_CANDIDATE_PRIO_SCAN 1000


struct rsn_pmksa_candidate {
	struct dl_list list;
	u8 bssid[ETH_ALEN];
	int priority;
};


/**
 * pmksa_candidate_free - Free all entries in PMKSA candidate list
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 */
void pmksa_candidate_free(struct wpa_sm *sm)
{
	struct rsn_pmksa_candidate *entry, *n;

	if (sm == NULL)
		return;

	dl_list_for_each_safe(entry, n, &sm->pmksa_candidates,
			      struct rsn_pmksa_candidate, list) {
		dl_list_del(&entry->list);
		os_free(entry);
	}
}


static void rsn_preauth_receive(void *ctx, const u8 *src_addr,
				const u8 *buf, size_t len)
{
	struct wpa_sm *sm = ctx;

	wpa_printf(MSG_DEBUG, "RX pre-auth from " MACSTR, MAC2STR(src_addr));
	wpa_hexdump(MSG_MSGDUMP, "RX pre-auth", buf, len);

	if (sm->preauth_eapol == NULL ||
	    is_zero_ether_addr(sm->preauth_bssid) ||
	    os_memcmp(sm->preauth_bssid, src_addr, ETH_ALEN) != 0) {
		wpa_printf(MSG_WARNING, "RSN pre-auth frame received from "
			   "unexpected source " MACSTR " - dropped",
			   MAC2STR(src_addr));
		return;
	}

	eapol_sm_rx_eapol(sm->preauth_eapol, src_addr, buf, len);
}


static void rsn_preauth_eapol_cb(struct eapol_sm *eapol, int success,
				 void *ctx)
{
	struct wpa_sm *sm = ctx;
	u8 pmk[PMK_LEN];

	if (success) {
		int res, pmk_len;
		pmk_len = PMK_LEN;
		res = eapol_sm_get_key(eapol, pmk, PMK_LEN);
		if (res) {
			/*
			 * EAP-LEAP is an exception from other EAP methods: it
			 * uses only 16-byte PMK.
			 */
			res = eapol_sm_get_key(eapol, pmk, 16);
			pmk_len = 16;
		}
		if (res == 0) {
			wpa_hexdump_key(MSG_DEBUG, "RSN: PMK from pre-auth",
					pmk, pmk_len);
			sm->pmk_len = pmk_len;
			pmksa_cache_add(sm->pmksa, pmk, pmk_len,
					sm->preauth_bssid, sm->own_addr,
					sm->network_ctx,
					WPA_KEY_MGMT_IEEE8021X);
		} else {
			wpa_msg(sm->ctx->msg_ctx, MSG_INFO,
				"RSN: failed to get master session key from "
				"pre-auth EAPOL state machines");
			success = 0;
		}
	}

	wpa_msg(sm->ctx->msg_ctx, MSG_INFO, "RSN: pre-authentication with "
		MACSTR " %s", MAC2STR(sm->preauth_bssid),
		success ? "completed successfully" : "failed");

	rsn_preauth_deinit(sm);
	rsn_preauth_candidate_process(sm);
}


static void rsn_preauth_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_sm *sm = eloop_ctx;

	wpa_msg(sm->ctx->msg_ctx, MSG_INFO, "RSN: pre-authentication with "
		MACSTR " timed out", MAC2STR(sm->preauth_bssid));
	rsn_preauth_deinit(sm);
	rsn_preauth_candidate_process(sm);
}


static int rsn_preauth_eapol_send(void *ctx, int type, const u8 *buf,
				  size_t len)
{
	struct wpa_sm *sm = ctx;
	u8 *msg;
	size_t msglen;
	int res;

	/* TODO: could add l2_packet_sendmsg that allows fragments to avoid
	 * extra copy here */

	if (sm->l2_preauth == NULL)
		return -1;

	msg = wpa_sm_alloc_eapol(sm, type, buf, len, &msglen, NULL);
	if (msg == NULL)
		return -1;

	wpa_hexdump(MSG_MSGDUMP, "TX EAPOL (preauth)", msg, msglen);
	res = l2_packet_send(sm->l2_preauth, sm->preauth_bssid,
			     ETH_P_RSN_PREAUTH, msg, msglen);
	os_free(msg);
	return res;
}


/**
 * rsn_preauth_init - Start new RSN pre-authentication
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @dst: Authenticator address (BSSID) with which to preauthenticate
 * @eap_conf: Current EAP configuration
 * Returns: 0 on success, -1 on another pre-authentication is in progress,
 * -2 on layer 2 packet initialization failure, -3 on EAPOL state machine
 * initialization failure, -4 on memory allocation failure
 *
 * This function request an RSN pre-authentication with a given destination
 * address. This is usually called for PMKSA candidates found from scan results
 * or from driver reports. In addition, ctrl_iface PREAUTH command can trigger
 * pre-authentication.
 */
int rsn_preauth_init(struct wpa_sm *sm, const u8 *dst,
		     struct eap_peer_config *eap_conf)
{
	struct eapol_config eapol_conf;
	struct eapol_ctx *ctx;

	if (sm->preauth_eapol)
		return -1;

	wpa_msg(sm->ctx->msg_ctx, MSG_DEBUG,
		"RSN: starting pre-authentication with " MACSTR, MAC2STR(dst));

	sm->l2_preauth = l2_packet_init(sm->ifname, sm->own_addr,
					ETH_P_RSN_PREAUTH,
					rsn_preauth_receive, sm, 0);
	if (sm->l2_preauth == NULL) {
		wpa_printf(MSG_WARNING, "RSN: Failed to initialize L2 packet "
			   "processing for pre-authentication");
		return -2;
	}

	if (sm->bridge_ifname) {
		sm->l2_preauth_br = l2_packet_init(sm->bridge_ifname,
						   sm->own_addr,
						   ETH_P_RSN_PREAUTH,
						   rsn_preauth_receive, sm, 0);
		if (sm->l2_preauth_br == NULL) {
			wpa_printf(MSG_WARNING, "RSN: Failed to initialize L2 "
				   "packet processing (bridge) for "
				   "pre-authentication");
			return -2;
		}
	}

	ctx = os_zalloc(sizeof(*ctx));
	if (ctx == NULL) {
		wpa_printf(MSG_WARNING, "Failed to allocate EAPOL context.");
		return -4;
	}
	ctx->ctx = sm->ctx->ctx;
	ctx->msg_ctx = sm->ctx->ctx;
	ctx->preauth = 1;
	ctx->cb = rsn_preauth_eapol_cb;
	ctx->cb_ctx = sm;
	ctx->scard_ctx = sm->scard_ctx;
	ctx->eapol_send = rsn_preauth_eapol_send;
	ctx->eapol_send_ctx = sm;
	ctx->set_config_blob = sm->ctx->set_config_blob;
	ctx->get_config_blob = sm->ctx->get_config_blob;

	sm->preauth_eapol = eapol_sm_init(ctx);
	if (sm->preauth_eapol == NULL) {
		os_free(ctx);
		wpa_printf(MSG_WARNING, "RSN: Failed to initialize EAPOL "
			   "state machines for pre-authentication");
		return -3;
	}
	os_memset(&eapol_conf, 0, sizeof(eapol_conf));
	eapol_conf.accept_802_1x_keys = 0;
	eapol_conf.required_keys = 0;
	eapol_conf.fast_reauth = sm->fast_reauth;
	eapol_conf.workaround = sm->eap_workaround;
	eapol_sm_notify_config(sm->preauth_eapol, eap_conf, &eapol_conf);
	/*
	 * Use a shorter startPeriod with preauthentication since the first
	 * preauth EAPOL-Start frame may end up being dropped due to race
	 * condition in the AP between the data receive and key configuration
	 * after the 4-Way Handshake.
	 */
	eapol_sm_configure(sm->preauth_eapol, -1, -1, 5, 6);
	os_memcpy(sm->preauth_bssid, dst, ETH_ALEN);

	eapol_sm_notify_portValid(sm->preauth_eapol, TRUE);
	/* 802.1X::portControl = Auto */
	eapol_sm_notify_portEnabled(sm->preauth_eapol, TRUE);

	eloop_register_timeout(sm->dot11RSNAConfigSATimeout, 0,
			       rsn_preauth_timeout, sm, NULL);

	return 0;
}


/**
 * rsn_preauth_deinit - Abort RSN pre-authentication
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 *
 * This function aborts the current RSN pre-authentication (if one is started)
 * and frees resources allocated for it.
 */
void rsn_preauth_deinit(struct wpa_sm *sm)
{
	if (sm == NULL || !sm->preauth_eapol)
		return;

	eloop_cancel_timeout(rsn_preauth_timeout, sm, NULL);
	eapol_sm_deinit(sm->preauth_eapol);
	sm->preauth_eapol = NULL;
	os_memset(sm->preauth_bssid, 0, ETH_ALEN);

	l2_packet_deinit(sm->l2_preauth);
	sm->l2_preauth = NULL;
	if (sm->l2_preauth_br) {
		l2_packet_deinit(sm->l2_preauth_br);
		sm->l2_preauth_br = NULL;
	}
}


/**
 * rsn_preauth_candidate_process - Process PMKSA candidates
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 *
 * Go through the PMKSA candidates and start pre-authentication if a candidate
 * without an existing PMKSA cache entry is found. Processed candidates will be
 * removed from the list.
 */
void rsn_preauth_candidate_process(struct wpa_sm *sm)
{
	struct rsn_pmksa_candidate *candidate, *n;

	if (dl_list_empty(&sm->pmksa_candidates))
		return;

	/* TODO: drop priority for old candidate entries */

	wpa_msg(sm->ctx->msg_ctx, MSG_DEBUG, "RSN: processing PMKSA candidate "
		"list");
	if (sm->preauth_eapol ||
	    sm->proto != WPA_PROTO_RSN ||
	    wpa_sm_get_state(sm) != WPA_COMPLETED ||
	    (sm->key_mgmt != WPA_KEY_MGMT_IEEE8021X &&
	     sm->key_mgmt != WPA_KEY_MGMT_IEEE8021X_SHA256)) {
		wpa_msg(sm->ctx->msg_ctx, MSG_DEBUG, "RSN: not in suitable "
			"state for new pre-authentication");
		return; /* invalid state for new pre-auth */
	}

	dl_list_for_each_safe(candidate, n, &sm->pmksa_candidates,
			      struct rsn_pmksa_candidate, list) {
		struct rsn_pmksa_cache_entry *p = NULL;
		p = pmksa_cache_get(sm->pmksa, candidate->bssid, NULL);
		if (os_memcmp(sm->bssid, candidate->bssid, ETH_ALEN) != 0 &&
		    (p == NULL || p->opportunistic)) {
			wpa_msg(sm->ctx->msg_ctx, MSG_DEBUG, "RSN: PMKSA "
				"candidate " MACSTR
				" selected for pre-authentication",
				MAC2STR(candidate->bssid));
			dl_list_del(&candidate->list);
			rsn_preauth_init(sm, candidate->bssid,
					 sm->eap_conf_ctx);
			os_free(candidate);
			return;
		}
		wpa_msg(sm->ctx->msg_ctx, MSG_DEBUG, "RSN: PMKSA candidate "
			MACSTR " does not need pre-authentication anymore",
			MAC2STR(candidate->bssid));
		/* Some drivers (e.g., NDIS) expect to get notified about the
		 * PMKIDs again, so report the existing data now. */
		if (p) {
			wpa_sm_add_pmkid(sm, candidate->bssid, p->pmkid);
		}

		dl_list_del(&candidate->list);
		os_free(candidate);
	}
	wpa_msg(sm->ctx->msg_ctx, MSG_DEBUG, "RSN: no more pending PMKSA "
		"candidates");
}


/**
 * pmksa_candidate_add - Add a new PMKSA candidate
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @bssid: BSSID (authenticator address) of the candidate
 * @prio: Priority (the smaller number, the higher priority)
 * @preauth: Whether the candidate AP advertises support for pre-authentication
 *
 * This function is used to add PMKSA candidates for RSN pre-authentication. It
 * is called from scan result processing and from driver events for PMKSA
 * candidates, i.e., EVENT_PMKID_CANDIDATE events to wpa_supplicant_event().
 */
void pmksa_candidate_add(struct wpa_sm *sm, const u8 *bssid,
			 int prio, int preauth)
{
	struct rsn_pmksa_candidate *cand, *pos;

	if (sm->network_ctx && sm->proactive_key_caching)
		pmksa_cache_get_opportunistic(sm->pmksa, sm->network_ctx,
					      bssid);

	if (!preauth) {
		wpa_printf(MSG_DEBUG, "RSN: Ignored PMKID candidate without "
			   "preauth flag");
		return;
	}

	/* If BSSID already on candidate list, update the priority of the old
	 * entry. Do not override priority based on normal scan results. */
	cand = NULL;
	dl_list_for_each(pos, &sm->pmksa_candidates,
			 struct rsn_pmksa_candidate, list) {
		if (os_memcmp(pos->bssid, bssid, ETH_ALEN) == 0) {
			cand = pos;
			break;
		}
	}

	if (cand) {
		dl_list_del(&cand->list);
		if (prio < PMKID_CANDIDATE_PRIO_SCAN)
			cand->priority = prio;
	} else {
		cand = os_zalloc(sizeof(*cand));
		if (cand == NULL)
			return;
		os_memcpy(cand->bssid, bssid, ETH_ALEN);
		cand->priority = prio;
	}

	/* Add candidate to the list; order by increasing priority value. i.e.,
	 * highest priority (smallest value) first. */
	dl_list_for_each(pos, &sm->pmksa_candidates,
			 struct rsn_pmksa_candidate, list) {
		if (cand->priority <= pos->priority) {
			dl_list_add(pos->list.prev, &cand->list);
			cand = NULL;
			break;
		}
	}
	if (cand)
		dl_list_add_tail(&sm->pmksa_candidates, &cand->list);

	wpa_msg(sm->ctx->msg_ctx, MSG_DEBUG, "RSN: added PMKSA cache "
		"candidate " MACSTR " prio %d", MAC2STR(bssid), prio);
	rsn_preauth_candidate_process(sm);
}


/* TODO: schedule periodic scans if current AP supports preauth */

/**
 * rsn_preauth_scan_results - Start processing scan results for canditates
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * Returns: 0 if ready to process results or -1 to skip processing
 *
 * This functions is used to notify RSN code about start of new scan results
 * processing. The actual scan results will be provided by calling
 * rsn_preauth_scan_result() for each BSS if this function returned 0.
 */
int rsn_preauth_scan_results(struct wpa_sm *sm)
{
	if (sm->ssid_len == 0)
		return -1;

	/*
	 * TODO: is it ok to free all candidates? What about the entries
	 * received from EVENT_PMKID_CANDIDATE?
	 */
	pmksa_candidate_free(sm);

	return 0;
}


/**
 * rsn_preauth_scan_result - Processing scan result for PMKSA canditates
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 *
 * Add all suitable APs (Authenticators) from scan results into PMKSA
 * candidate list.
 */
void rsn_preauth_scan_result(struct wpa_sm *sm, const u8 *bssid,
			     const u8 *ssid, const u8 *rsn)
{
	struct wpa_ie_data ie;
	struct rsn_pmksa_cache_entry *pmksa;

	if (ssid[1] != sm->ssid_len ||
	    os_memcmp(ssid + 2, sm->ssid, sm->ssid_len) != 0)
		return; /* Not for the current SSID */

	if (os_memcmp(bssid, sm->bssid, ETH_ALEN) == 0)
		return; /* Ignore current AP */

	if (wpa_parse_wpa_ie(rsn, 2 + rsn[1], &ie))
		return;

	pmksa = pmksa_cache_get(sm->pmksa, bssid, NULL);
	if (pmksa && (!pmksa->opportunistic ||
		      !(ie.capabilities & WPA_CAPABILITY_PREAUTH)))
		return;

	/* Give less priority to candidates found from normal scan results. */
	pmksa_candidate_add(sm, bssid, PMKID_CANDIDATE_PRIO_SCAN,
			    ie.capabilities & WPA_CAPABILITY_PREAUTH);
}


#ifdef CONFIG_CTRL_IFACE
/**
 * rsn_preauth_get_status - Get pre-authentication status
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 * @buf: Buffer for status information
 * @buflen: Maximum buffer length
 * @verbose: Whether to include verbose status information
 * Returns: Number of bytes written to buf.
 *
 * Query WPA2 pre-authentication for status information. This function fills in
 * a text area with current status information. If the buffer (buf) is not
 * large enough, status information will be truncated to fit the buffer.
 */
int rsn_preauth_get_status(struct wpa_sm *sm, char *buf, size_t buflen,
			   int verbose)
{
	char *pos = buf, *end = buf + buflen;
	int res, ret;

	if (sm->preauth_eapol) {
		ret = os_snprintf(pos, end - pos, "Pre-authentication "
				  "EAPOL state machines:\n");
		if (ret < 0 || ret >= end - pos)
			return pos - buf;
		pos += ret;
		res = eapol_sm_get_status(sm->preauth_eapol,
					  pos, end - pos, verbose);
		if (res >= 0)
			pos += res;
	}

	return pos - buf;
}
#endif /* CONFIG_CTRL_IFACE */


/**
 * rsn_preauth_in_progress - Verify whether pre-authentication is in progress
 * @sm: Pointer to WPA state machine data from wpa_sm_init()
 */
int rsn_preauth_in_progress(struct wpa_sm *sm)
{
	return sm->preauth_eapol != NULL;
}

#endif /* IEEE8021X_EAPOL and !CONFIG_NO_WPA2 */
