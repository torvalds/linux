/*
 * hostapd / AP table
 * Copyright (c) 2002-2003, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2003-2004, Instant802 Networks, Inc.
 * Copyright (c) 2006, Devicescape Software, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef AP_LIST_H
#define AP_LIST_H

struct ap_info {
	/* Note: next/prev pointers are updated whenever a new beacon is
	 * received because these are used to find the least recently used
	 * entries. */
	struct ap_info *next; /* next entry in AP list */
	struct ap_info *prev; /* previous entry in AP list */
	struct ap_info *hnext; /* next entry in hash table list */
	u8 addr[6];
	u8 supported_rates[WLAN_SUPP_RATES_MAX];
	int erp; /* ERP Info or -1 if ERP info element not present */

	int channel;

	int ht_support;

	struct os_reltime last_beacon;
};

struct ieee802_11_elems;
struct hostapd_frame_info;

void ap_list_process_beacon(struct hostapd_iface *iface,
			    const struct ieee80211_mgmt *mgmt,
			    struct ieee802_11_elems *elems,
			    struct hostapd_frame_info *fi);
#ifdef NEED_AP_MLME
int ap_list_init(struct hostapd_iface *iface);
void ap_list_deinit(struct hostapd_iface *iface);
void ap_list_timer(struct hostapd_iface *iface);
#else /* NEED_AP_MLME */
static inline int ap_list_init(struct hostapd_iface *iface)
{
	return 0;
}

static inline void ap_list_deinit(struct hostapd_iface *iface)
{
}

static inline void ap_list_timer(struct hostapd_iface *iface)
{
}
#endif /* NEED_AP_MLME */

#endif /* AP_LIST_H */
