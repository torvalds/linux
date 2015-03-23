/*
 * hostapd - WPA/RSN IE and KDE definitions
 * Copyright (c) 2004-2007, Jouni Malinen <j@w1.fi>
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

#ifndef WPA_AUTH_IE_H
#define WPA_AUTH_IE_H

struct wpa_eapol_ie_parse {
	const u8 *wpa_ie;
	size_t wpa_ie_len;
	const u8 *rsn_ie;
	size_t rsn_ie_len;
	const u8 *pmkid;
	const u8 *gtk;
	size_t gtk_len;
	const u8 *mac_addr;
	size_t mac_addr_len;
#ifdef CONFIG_PEERKEY
	const u8 *smk;
	size_t smk_len;
	const u8 *nonce;
	size_t nonce_len;
	const u8 *lifetime;
	size_t lifetime_len;
	const u8 *error;
	size_t error_len;
#endif /* CONFIG_PEERKEY */
#ifdef CONFIG_IEEE80211W
	const u8 *igtk;
	size_t igtk_len;
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_IEEE80211R
	const u8 *mdie;
	size_t mdie_len;
	const u8 *ftie;
	size_t ftie_len;
#endif /* CONFIG_IEEE80211R */
};

int wpa_parse_kde_ies(const u8 *buf, size_t len,
		      struct wpa_eapol_ie_parse *ie);
u8 * wpa_add_kde(u8 *pos, u32 kde, const u8 *data, size_t data_len,
		 const u8 *data2, size_t data2_len);
int wpa_auth_gen_wpa_ie(struct wpa_authenticator *wpa_auth);

#endif /* WPA_AUTH_IE_H */
