/*
 * WPA Supplicant - Windows/NDIS driver interface
 * Copyright (c) 2004-2006, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef DRIVER_NDIS_H
#define DRIVER_NDIS_H

#ifdef CONFIG_NDIS_EVENTS_INTEGRATED
struct ndis_events_data;
struct ndis_events_data * ndis_events_init(HANDLE *read_pipe, HANDLE *event,
					   const char *ifname,
					   const char *desc);
void ndis_events_deinit(struct ndis_events_data *events);
#endif /* CONFIG_NDIS_EVENTS_INTEGRATED */

struct ndis_pmkid_entry {
	struct ndis_pmkid_entry *next;
	u8 bssid[ETH_ALEN];
	u8 pmkid[16];
};

struct wpa_driver_ndis_data {
	void *ctx;
	char ifname[100]; /* GUID: {7EE3EFE5-C165-472F-986D-F6FBEDFE8C8D} */
#ifdef _WIN32_WCE
	TCHAR *adapter_name;
	HANDLE event_queue; /* NDISUIO notifier MsgQueue */
	HANDLE connected_event; /* WpaSupplicantConnected event */
#endif /* _WIN32_WCE */
	u8 own_addr[ETH_ALEN];
#ifdef CONFIG_USE_NDISUIO
	HANDLE ndisuio;
#else /* CONFIG_USE_NDISUIO */
	LPADAPTER adapter;
#endif /* CONFIG_USE_NDISUIO */
	u8 bssid[ETH_ALEN];

	int has_capability;
	int no_of_pmkid;
	int radio_enabled;
	struct wpa_driver_capa capa;
	struct ndis_pmkid_entry *pmkid;
	char *adapter_desc;
	int wired;
	int native80211;
	int mode;
	int wzc_disabled;
	int oid_bssid_set;
#ifdef CONFIG_NDIS_EVENTS_INTEGRATED
	HANDLE events_pipe, event_avail;
	struct ndis_events_data *events;
#endif /* CONFIG_NDIS_EVENTS_INTEGRATED */
};

#endif /* DRIVER_NDIS_H */
