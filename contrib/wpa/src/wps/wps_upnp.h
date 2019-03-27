/*
 * UPnP WPS Device
 * Copyright (c) 2000-2003 Intel Corporation
 * Copyright (c) 2006-2007 Sony Corporation
 * Copyright (c) 2008-2009 Atheros Communications
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * See wps_upnp.c for more details on licensing and code history.
 */

#ifndef WPS_UPNP_H
#define WPS_UPNP_H

#include "utils/list.h"

struct upnp_wps_device_sm;
struct wps_context;
struct wps_data;

struct upnp_wps_peer {
	struct dl_list list;
	struct wps_data *wps;
};

enum upnp_wps_wlanevent_type {
	UPNP_WPS_WLANEVENT_TYPE_PROBE = 1,
	UPNP_WPS_WLANEVENT_TYPE_EAP = 2
};

struct upnp_wps_device_ctx {
	int (*rx_req_put_wlan_response)(
		void *priv, enum upnp_wps_wlanevent_type ev_type,
		const u8 *mac_addr, const struct wpabuf *msg,
		enum wps_msg_type msg_type);

	char *ap_pin;
};

struct upnp_wps_device_sm *
upnp_wps_device_init(struct upnp_wps_device_ctx *ctx, struct wps_context *wps,
		     void *priv, char *net_if);
void upnp_wps_device_deinit(struct upnp_wps_device_sm *sm, void *priv);

int upnp_wps_device_send_wlan_event(struct upnp_wps_device_sm *sm,
				    const u8 from_mac_addr[ETH_ALEN],
				    enum upnp_wps_wlanevent_type ev_type,
				    const struct wpabuf *msg);
int upnp_wps_subscribers(struct upnp_wps_device_sm *sm);
int upnp_wps_set_ap_pin(struct upnp_wps_device_sm *sm, const char *ap_pin);

#endif /* WPS_UPNP_H */
