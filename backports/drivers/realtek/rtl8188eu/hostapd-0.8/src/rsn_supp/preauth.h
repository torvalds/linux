/*
 * wpa_supplicant - WPA2/RSN pre-authentication functions
 * Copyright (c) 2003-2009, Jouni Malinen <j@w1.fi>
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

#ifndef PREAUTH_H
#define PREAUTH_H

struct wpa_scan_results;

#if defined(IEEE8021X_EAPOL) && !defined(CONFIG_NO_WPA2)

void pmksa_candidate_free(struct wpa_sm *sm);
int rsn_preauth_init(struct wpa_sm *sm, const u8 *dst,
		     struct eap_peer_config *eap_conf);
void rsn_preauth_deinit(struct wpa_sm *sm);
int rsn_preauth_scan_results(struct wpa_sm *sm);
void rsn_preauth_scan_result(struct wpa_sm *sm, const u8 *bssid,
			     const u8 *ssid, const u8 *rsn);
void pmksa_candidate_add(struct wpa_sm *sm, const u8 *bssid,
			 int prio, int preauth);
void rsn_preauth_candidate_process(struct wpa_sm *sm);
int rsn_preauth_get_status(struct wpa_sm *sm, char *buf, size_t buflen,
			   int verbose);
int rsn_preauth_in_progress(struct wpa_sm *sm);

#else /* IEEE8021X_EAPOL and !CONFIG_NO_WPA2 */

static inline void pmksa_candidate_free(struct wpa_sm *sm)
{
}

static inline void rsn_preauth_candidate_process(struct wpa_sm *sm)
{
}

static inline int rsn_preauth_init(struct wpa_sm *sm, const u8 *dst,
				   struct eap_peer_config *eap_conf)
{
	return -1;
}

static inline void rsn_preauth_deinit(struct wpa_sm *sm)
{
}

static inline int rsn_preauth_scan_results(struct wpa_sm *sm)
{
	return -1;
}

static inline void rsn_preauth_scan_result(struct wpa_sm *sm, const u8 *bssid,
					   const u8 *ssid, const u8 *rsn)
{
}

static inline void pmksa_candidate_add(struct wpa_sm *sm,
				       const u8 *bssid,
				       int prio, int preauth)
{
}

static inline int rsn_preauth_get_status(struct wpa_sm *sm, char *buf,
					 size_t buflen, int verbose)
{
	return 0;
}

static inline int rsn_preauth_in_progress(struct wpa_sm *sm)
{
	return 0;
}

#endif /* IEEE8021X_EAPOL and !CONFIG_NO_WPA2 */

#endif /* PREAUTH_H */
