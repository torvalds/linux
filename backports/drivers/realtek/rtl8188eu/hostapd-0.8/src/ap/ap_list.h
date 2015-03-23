/*
 * hostapd / AP table
 * Copyright (c) 2002-2003, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2003-2004, Instant802 Networks, Inc.
 * Copyright (c) 2006, Devicescape Software, Inc.
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

#ifndef AP_LIST_H
#define AP_LIST_H

struct ap_info {
	/* Note: next/prev pointers are updated whenever a new beacon is
	 * received because these are used to find the least recently used
	 * entries. iter_next/iter_prev are updated only when adding new BSSes
	 * and when removing old ones. These should be used when iterating
	 * through the table in a manner that allows beacons to be received
	 * during the iteration. */
	struct ap_info *next; /* next entry in AP list */
	struct ap_info *prev; /* previous entry in AP list */
	struct ap_info *hnext; /* next entry in hash table list */
	struct ap_info *iter_next; /* next entry in AP iteration list */
	struct ap_info *iter_prev; /* previous entry in AP iteration list */
	u8 addr[6];
	u16 beacon_int;
	u16 capability;
	u8 supported_rates[WLAN_SUPP_RATES_MAX];
	u8 ssid[33];
	size_t ssid_len;
	int wpa;
	int erp; /* ERP Info or -1 if ERP info element not present */

	int channel;
	int datarate; /* in 100 kbps */
	int ssi_signal;

	int ht_support;

	unsigned int num_beacons; /* number of beacon frames received */
	os_time_t last_beacon;

	int already_seen; /* whether API call AP-NEW has already fetched
			   * information about this AP */
};

struct ieee802_11_elems;
struct hostapd_frame_info;

struct ap_info * ap_get_ap(struct hostapd_iface *iface, const u8 *sta);
int ap_ap_for_each(struct hostapd_iface *iface,
		   int (*func)(struct ap_info *s, void *data), void *data);
void ap_list_process_beacon(struct hostapd_iface *iface,
			    const struct ieee80211_mgmt *mgmt,
			    struct ieee802_11_elems *elems,
			    struct hostapd_frame_info *fi);
#ifdef NEED_AP_MLME
int ap_list_init(struct hostapd_iface *iface);
void ap_list_deinit(struct hostapd_iface *iface);
#else /* NEED_AP_MLME */
static inline int ap_list_init(struct hostapd_iface *iface)
{
	return 0;
}

static inline void ap_list_deinit(struct hostapd_iface *iface)
{
}
#endif /* NEED_AP_MLME */

#endif /* AP_LIST_H */
