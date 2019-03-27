/*
 * WPA Supplicant - Basic mesh peer management
 * Copyright (c) 2013-2014, cozybit, Inc.  All rights reserved.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef MESH_MPM_H
#define MESH_MPM_H

/* notify MPM of new mesh peer to be inserted in MPM and driver */
void wpa_mesh_new_mesh_peer(struct wpa_supplicant *wpa_s, const u8 *addr,
			    struct ieee802_11_elems *elems);
void mesh_mpm_deinit(struct wpa_supplicant *wpa_s, struct hostapd_iface *ifmsh);
void mesh_mpm_auth_peer(struct wpa_supplicant *wpa_s, const u8 *addr);
void mesh_mpm_free_sta(struct hostapd_data *hapd, struct sta_info *sta);
void wpa_mesh_set_plink_state(struct wpa_supplicant *wpa_s,
			      struct sta_info *sta,
			      enum mesh_plink_state state);
int mesh_mpm_close_peer(struct wpa_supplicant *wpa_s, const u8 *addr);
int mesh_mpm_connect_peer(struct wpa_supplicant *wpa_s, const u8 *addr,
			  int duration);

#ifdef CONFIG_MESH

void mesh_mpm_action_rx(struct wpa_supplicant *wpa_s,
			const struct ieee80211_mgmt *mgmt, size_t len);
void mesh_mpm_mgmt_rx(struct wpa_supplicant *wpa_s, struct rx_mgmt *rx_mgmt);

#else /* CONFIG_MESH */

static inline void mesh_mpm_action_rx(struct wpa_supplicant *wpa_s,
				      const struct ieee80211_mgmt *mgmt,
				      size_t len)
{
}

static inline void mesh_mpm_mgmt_rx(struct wpa_supplicant *wpa_s,
				    struct rx_mgmt *rx_mgmt)
{
}

#endif /* CONFIG_MESH */

#endif /* MESH_MPM_H */
