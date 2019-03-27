/*
 * IEEE 802.1X-2010 KaY Interface
 * Copyright (c) 2013-2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPAS_KAY_H
#define WPAS_KAY_H

#ifdef CONFIG_MACSEC

int ieee802_1x_alloc_kay_sm(struct wpa_supplicant *wpa_s,
			    struct wpa_ssid *ssid);
void * ieee802_1x_notify_create_actor(struct wpa_supplicant *wpa_s,
				      const u8 *peer_addr);
void ieee802_1x_dealloc_kay_sm(struct wpa_supplicant *wpa_s);

void * ieee802_1x_create_preshared_mka(struct wpa_supplicant *wpa_s,
				       struct wpa_ssid *ssid);

#else /* CONFIG_MACSEC */

static inline int ieee802_1x_alloc_kay_sm(struct wpa_supplicant *wpa_s,
					  struct wpa_ssid *ssid)
{
	return 0;
}

static inline void *
ieee802_1x_notify_create_actor(struct wpa_supplicant *wpa_s,
			       const u8 *peer_addr)
{
	return NULL;
}

static inline void ieee802_1x_dealloc_kay_sm(struct wpa_supplicant *wpa_s)
{
}

static inline void *
ieee802_1x_create_preshared_mka(struct wpa_supplicant *wpa_s,
				struct wpa_ssid *ssid)
{
	return 0;
}

#endif /* CONFIG_MACSEC */

#endif /* WPAS_KAY_H */
