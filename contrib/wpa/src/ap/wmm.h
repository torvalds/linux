/*
 * hostapd / WMM (Wi-Fi Multimedia)
 * Copyright 2002-2003, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WME_H
#define WME_H

struct ieee80211_mgmt;
struct wmm_tspec_element;

u8 * hostapd_eid_wmm(struct hostapd_data *hapd, u8 *eid);
int hostapd_eid_wmm_valid(struct hostapd_data *hapd, const u8 *eid,
			  size_t len);
void hostapd_wmm_action(struct hostapd_data *hapd,
			const struct ieee80211_mgmt *mgmt, size_t len);
int wmm_process_tspec(struct wmm_tspec_element *tspec);

#endif /* WME_H */
