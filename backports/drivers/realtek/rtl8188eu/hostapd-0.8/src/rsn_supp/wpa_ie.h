/*
 * wpa_supplicant - WPA/RSN IE and KDE definitions
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

#ifndef WPA_IE_H
#define WPA_IE_H

struct wpa_sm;

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
	const u8 *mdie;
	size_t mdie_len;
	const u8 *ftie;
	size_t ftie_len;
	const u8 *reassoc_deadline;
	const u8 *key_lifetime;
	const u8 *lnkid;
	size_t lnkid_len;
	const u8 *ext_capab;
	size_t ext_capab_len;
};

int wpa_supplicant_parse_ies(const u8 *buf, size_t len,
			     struct wpa_eapol_ie_parse *ie);
int wpa_gen_wpa_ie(struct wpa_sm *sm, u8 *wpa_ie, size_t wpa_ie_len);

#endif /* WPA_IE_H */
