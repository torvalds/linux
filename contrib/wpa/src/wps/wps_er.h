/*
 * Wi-Fi Protected Setup - External Registrar
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPS_ER_H
#define WPS_ER_H

#include "utils/list.h"

struct wps_er_sta {
	struct dl_list list;
	struct wps_er_ap *ap;
	u8 addr[ETH_ALEN];
	u16 config_methods;
	u8 uuid[WPS_UUID_LEN];
	u8 pri_dev_type[8];
	u16 dev_passwd_id;
	int m1_received;
	char *manufacturer;
	char *model_name;
	char *model_number;
	char *serial_number;
	char *dev_name;
	struct wps_data *wps;
	struct http_client *http;
	struct wps_credential *cred;
};

struct wps_er_ap {
	struct dl_list list;
	struct wps_er *er;
	struct dl_list sta; /* list of STAs/Enrollees using this AP */
	struct in_addr addr;
	char *location;
	struct http_client *http;
	struct wps_data *wps;

	u8 uuid[WPS_UUID_LEN];
	u8 pri_dev_type[8];
	u8 wps_state;
	u8 mac_addr[ETH_ALEN];
	char *friendly_name;
	char *manufacturer;
	char *manufacturer_url;
	char *model_description;
	char *model_name;
	char *model_number;
	char *model_url;
	char *serial_number;
	char *udn;
	char *upc;

	char *scpd_url;
	char *control_url;
	char *event_sub_url;

	int subscribed;
	u8 sid[WPS_UUID_LEN];
	unsigned int id;

	struct wps_credential *ap_settings;

	void (*m1_handler)(struct wps_er_ap *ap, struct wpabuf *m1);
};

struct wps_er_ap_settings {
	struct dl_list list;
	u8 uuid[WPS_UUID_LEN];
	struct wps_credential ap_settings;
};

struct wps_er {
	struct wps_context *wps;
	char ifname[17];
	int forced_ifname;
	u8 mac_addr[ETH_ALEN]; /* mac addr of network i.f. we use */
	char *ip_addr_text; /* IP address of network i.f. we use */
	unsigned ip_addr; /* IP address of network i.f. we use (host order) */
	int multicast_sd;
	int ssdp_sd;
	struct dl_list ap;
	struct dl_list ap_unsubscribing;
	struct dl_list ap_settings;
	struct http_server *http_srv;
	int http_port;
	unsigned int next_ap_id;
	unsigned int event_id;
	int deinitializing;
	void (*deinit_done_cb)(void *ctx);
	void *deinit_done_ctx;
	struct in_addr filter_addr;
	int skip_set_sel_reg;
	const u8 *set_sel_reg_uuid_filter;
};


/* wps_er.c */
void wps_er_ap_add(struct wps_er *er, const u8 *uuid, struct in_addr *addr,
		   const char *location, int max_age);
void wps_er_ap_remove(struct wps_er *er, struct in_addr *addr);
int wps_er_ap_cache_settings(struct wps_er *er, struct in_addr *addr);

/* wps_er_ssdp.c */
int wps_er_ssdp_init(struct wps_er *er);
void wps_er_ssdp_deinit(struct wps_er *er);
void wps_er_send_ssdp_msearch(struct wps_er *er);

#endif /* WPS_ER_H */
