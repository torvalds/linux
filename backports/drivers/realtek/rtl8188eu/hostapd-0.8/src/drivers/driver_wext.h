/*
 * WPA Supplicant - driver_wext exported functions
 * Copyright (c) 2003-2005, Jouni Malinen <j@w1.fi>
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

#ifndef DRIVER_WEXT_H
#define DRIVER_WEXT_H

#include <net/if.h>

struct wpa_driver_wext_data {
	void *ctx;
	struct netlink_data *netlink;
	int ioctl_sock;
	int mlme_sock;
	char ifname[IFNAMSIZ + 1];
	char phyname[32];
	int ifindex;
	int ifindex2;
	int if_removed;
	int if_disabled;
	struct rfkill_data *rfkill;
	u8 *assoc_req_ies;
	size_t assoc_req_ies_len;
	u8 *assoc_resp_ies;
	size_t assoc_resp_ies_len;
	struct wpa_driver_capa capa;
	int has_capability;
	int we_version_compiled;

	/* for set_auth_alg fallback */
	int use_crypt;
	int auth_alg_fallback;

	int operstate;

	char mlmedev[IFNAMSIZ + 1];

	int scan_complete_events;

	int cfg80211; /* whether driver is using cfg80211 */

	u8 max_level;
};

int wpa_driver_wext_get_bssid(void *priv, u8 *bssid);
int wpa_driver_wext_set_bssid(void *priv, const u8 *bssid);
int wpa_driver_wext_get_ssid(void *priv, u8 *ssid);
int wpa_driver_wext_set_ssid(void *priv, const u8 *ssid, size_t ssid_len);
int wpa_driver_wext_set_freq(void *priv, int freq);
int wpa_driver_wext_set_mode(void *priv, int mode);
int wpa_driver_wext_set_key(const char *ifname, void *priv, enum wpa_alg alg,
			    const u8 *addr, int key_idx,
			    int set_tx, const u8 *seq, size_t seq_len,
			    const u8 *key, size_t key_len);
int wpa_driver_wext_scan(void *priv, struct wpa_driver_scan_params *params);
struct wpa_scan_results * wpa_driver_wext_get_scan_results(void *priv);

void wpa_driver_wext_scan_timeout(void *eloop_ctx, void *timeout_ctx);

int wpa_driver_wext_alternative_ifindex(struct wpa_driver_wext_data *drv,
					const char *ifname);

void * wpa_driver_wext_init(void *ctx, const char *ifname);
void wpa_driver_wext_deinit(void *priv);

int wpa_driver_wext_set_operstate(void *priv, int state);
int wpa_driver_wext_get_version(struct wpa_driver_wext_data *drv);

int wpa_driver_wext_associate(void *priv,
			      struct wpa_driver_associate_params *params);
int wpa_driver_wext_get_capa(void *priv, struct wpa_driver_capa *capa);
int wpa_driver_wext_set_auth_param(struct wpa_driver_wext_data *drv,
				   int idx, u32 value);
int wpa_driver_wext_cipher2wext(int cipher);
int wpa_driver_wext_keymgmt2wext(int keymgmt);

#endif /* DRIVER_WEXT_H */
