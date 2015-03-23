/*
 * Wi-Fi Protected Setup - External Registrar
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
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

#include "includes.h"

#include "common.h"
#include "base64.h"
#include "uuid.h"
#include "eloop.h"
#include "httpread.h"
#include "http_client.h"
#include "http_server.h"
#include "upnp_xml.h"
#include "wps_i.h"
#include "wps_upnp.h"
#include "wps_upnp_i.h"
#include "wps_er.h"


static void wps_er_deinit_finish(void *eloop_data, void *user_ctx);
static void wps_er_ap_timeout(void *eloop_data, void *user_ctx);
static void wps_er_sta_timeout(void *eloop_data, void *user_ctx);
static void wps_er_ap_process(struct wps_er_ap *ap, struct wpabuf *msg);
static int wps_er_send_get_device_info(struct wps_er_ap *ap,
				       void (*m1_handler)(struct wps_er_ap *ap,
							  struct wpabuf *m1));


static void wps_er_sta_event(struct wps_context *wps, struct wps_er_sta *sta,
			     enum wps_event event)
{
	union wps_event_data data;
	struct wps_event_er_enrollee *ev = &data.enrollee;

	if (wps->event_cb == NULL)
		return;

	os_memset(&data, 0, sizeof(data));
	ev->uuid = sta->uuid;
	ev->mac_addr = sta->addr;
	ev->m1_received = sta->m1_received;
	ev->config_methods = sta->config_methods;
	ev->dev_passwd_id = sta->dev_passwd_id;
	ev->pri_dev_type = sta->pri_dev_type;
	ev->dev_name = sta->dev_name;
	ev->manufacturer = sta->manufacturer;
	ev->model_name = sta->model_name;
	ev->model_number = sta->model_number;
	ev->serial_number = sta->serial_number;
	wps->event_cb(wps->cb_ctx, event, &data);
}


static struct wps_er_sta * wps_er_sta_get(struct wps_er_ap *ap, const u8 *addr,
					  const u8 *uuid)
{
	struct wps_er_sta *sta;
	dl_list_for_each(sta, &ap->sta, struct wps_er_sta, list) {
		if ((addr == NULL ||
		     os_memcmp(sta->addr, addr, ETH_ALEN) == 0) &&
		    (uuid == NULL ||
		     os_memcmp(uuid, sta->uuid, WPS_UUID_LEN) == 0))
			return sta;
	}
	return NULL;
}


static void wps_er_sta_free(struct wps_er_sta *sta)
{
	wps_er_sta_event(sta->ap->er->wps, sta, WPS_EV_ER_ENROLLEE_REMOVE);
	if (sta->wps)
		wps_deinit(sta->wps);
	os_free(sta->manufacturer);
	os_free(sta->model_name);
	os_free(sta->model_number);
	os_free(sta->serial_number);
	os_free(sta->dev_name);
	http_client_free(sta->http);
	eloop_cancel_timeout(wps_er_sta_timeout, sta, NULL);
	os_free(sta->cred);
	os_free(sta);
}


static void wps_er_sta_remove_all(struct wps_er_ap *ap)
{
	struct wps_er_sta *prev, *sta;
	dl_list_for_each_safe(sta, prev, &ap->sta, struct wps_er_sta, list)
		wps_er_sta_free(sta);
}


static struct wps_er_ap * wps_er_ap_get(struct wps_er *er,
					struct in_addr *addr, const u8 *uuid)
{
	struct wps_er_ap *ap;
	dl_list_for_each(ap, &er->ap, struct wps_er_ap, list) {
		if ((addr == NULL || ap->addr.s_addr == addr->s_addr) &&
		    (uuid == NULL ||
		     os_memcmp(uuid, ap->uuid, WPS_UUID_LEN) == 0))
			return ap;
	}
	return NULL;
}


static struct wps_er_ap * wps_er_ap_get_id(struct wps_er *er, unsigned int id)
{
	struct wps_er_ap *ap;
	dl_list_for_each(ap, &er->ap, struct wps_er_ap, list) {
		if (ap->id == id)
			return ap;
	}
	return NULL;
}


static void wps_er_ap_event(struct wps_context *wps, struct wps_er_ap *ap,
			    enum wps_event event)
{
	union wps_event_data data;
	struct wps_event_er_ap *evap = &data.ap;

	if (wps->event_cb == NULL)
		return;

	os_memset(&data, 0, sizeof(data));
	evap->uuid = ap->uuid;
	evap->friendly_name = ap->friendly_name;
	evap->manufacturer = ap->manufacturer;
	evap->manufacturer_url = ap->manufacturer_url;
	evap->model_description = ap->model_description;
	evap->model_name = ap->model_name;
	evap->model_number = ap->model_number;
	evap->model_url = ap->model_url;
	evap->serial_number = ap->serial_number;
	evap->upc = ap->upc;
	evap->pri_dev_type = ap->pri_dev_type;
	evap->wps_state = ap->wps_state;
	evap->mac_addr = ap->mac_addr;
	wps->event_cb(wps->cb_ctx, event, &data);
}


static void wps_er_ap_free(struct wps_er_ap *ap)
{
	http_client_free(ap->http);
	ap->http = NULL;

	os_free(ap->location);
	os_free(ap->friendly_name);
	os_free(ap->manufacturer);
	os_free(ap->manufacturer_url);
	os_free(ap->model_description);
	os_free(ap->model_name);
	os_free(ap->model_number);
	os_free(ap->model_url);
	os_free(ap->serial_number);
	os_free(ap->udn);
	os_free(ap->upc);

	os_free(ap->scpd_url);
	os_free(ap->control_url);
	os_free(ap->event_sub_url);

	os_free(ap->ap_settings);

	os_free(ap);
}


static void wps_er_ap_unsubscribed(struct wps_er *er, struct wps_er_ap *ap)
{
	wpa_printf(MSG_DEBUG, "WPS ER: Unsubscribed from AP %s (%s)",
		   inet_ntoa(ap->addr), ap->location);
	dl_list_del(&ap->list);
	wps_er_ap_free(ap);

	if (er->deinitializing && dl_list_empty(&er->ap_unsubscribing)) {
		eloop_cancel_timeout(wps_er_deinit_finish, er, NULL);
		wps_er_deinit_finish(er, NULL);
	}
}


static void wps_er_http_unsubscribe_cb(void *ctx, struct http_client *c,
				       enum http_client_event event)
{
	struct wps_er_ap *ap = ctx;

	switch (event) {
	case HTTP_CLIENT_OK:
		wpa_printf(MSG_DEBUG, "WPS ER: Unsubscribed from events");
		ap->subscribed = 0;
		break;
	case HTTP_CLIENT_FAILED:
	case HTTP_CLIENT_INVALID_REPLY:
	case HTTP_CLIENT_TIMEOUT:
		wpa_printf(MSG_DEBUG, "WPS ER: Failed to unsubscribe from "
			   "events");
		break;
	}
	http_client_free(ap->http);
	ap->http = NULL;

	/*
	 * Need to get rid of the AP entry regardless of whether we managed to
	 * unsubscribe cleanly or not.
	 */
	wps_er_ap_unsubscribed(ap->er, ap);
}


static void wps_er_ap_unsubscribe(struct wps_er *er, struct wps_er_ap *ap)
{
	struct wpabuf *req;
	struct sockaddr_in dst;
	char *url, *path;
	char sid[100];

	if (ap->event_sub_url == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: No eventSubURL - cannot "
			   "subscribe");
		goto fail;
	}
	if (ap->http) {
		wpa_printf(MSG_DEBUG, "WPS ER: Pending HTTP request - cannot "
			   "send subscribe request");
		goto fail;
	}

	url = http_client_url_parse(ap->event_sub_url, &dst, &path);
	if (url == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: Failed to parse eventSubURL");
		goto fail;
	}

	req = wpabuf_alloc(os_strlen(ap->event_sub_url) + 1000);
	if (req == NULL) {
		os_free(url);
		goto fail;
	}
	uuid_bin2str(ap->sid, sid, sizeof(sid));
	wpabuf_printf(req,
		      "UNSUBSCRIBE %s HTTP/1.1\r\n"
		      "HOST: %s:%d\r\n"
		      "SID: uuid:%s\r\n"
		      "\r\n",
		      path, inet_ntoa(dst.sin_addr), ntohs(dst.sin_port), sid);
	os_free(url);
	wpa_hexdump_ascii(MSG_MSGDUMP, "WPS ER: Unsubscription request",
			  wpabuf_head(req), wpabuf_len(req));

	ap->http = http_client_addr(&dst, req, 1000,
				    wps_er_http_unsubscribe_cb, ap);
	if (ap->http == NULL) {
		wpabuf_free(req);
		goto fail;
	}
	return;

fail:
	/*
	 * Need to get rid of the AP entry even when we fail to unsubscribe
	 * cleanly.
	 */
	wps_er_ap_unsubscribed(ap->er, ap);
}


static struct wps_er_ap_settings * wps_er_ap_get_settings(struct wps_er *er,
							  const u8 *uuid)
{
	struct wps_er_ap_settings *s;
	dl_list_for_each(s, &er->ap_settings, struct wps_er_ap_settings, list)
		if (os_memcmp(uuid, s->uuid, WPS_UUID_LEN) == 0)
			return s;
	return NULL;
}


int wps_er_ap_cache_settings(struct wps_er *er, struct in_addr *addr)
{
	struct wps_er_ap *ap;
	struct wps_er_ap_settings *settings;

	ap = wps_er_ap_get(er, addr, NULL);
	if (ap == NULL || ap->ap_settings == NULL)
		return -1;

	settings = wps_er_ap_get_settings(er, ap->uuid);
	if (!settings) {
		settings = os_zalloc(sizeof(*settings));
		if (settings == NULL)
			return -1;
		os_memcpy(settings->uuid, ap->uuid, WPS_UUID_LEN);
		dl_list_add(&er->ap_settings, &settings->list);
	}
	os_memcpy(&settings->ap_settings, ap->ap_settings,
		  sizeof(struct wps_credential));

	return 0;
}


static int wps_er_ap_use_cached_settings(struct wps_er *er,
					 struct wps_er_ap *ap)
{
	struct wps_er_ap_settings *s;

	if (ap->ap_settings)
		return 0;

	s = wps_er_ap_get_settings(ap->er, ap->uuid);
	if (!s)
		return -1;

	ap->ap_settings = os_malloc(sizeof(*ap->ap_settings));
	if (ap->ap_settings == NULL)
		return -1;

	os_memcpy(ap->ap_settings, &s->ap_settings, sizeof(*ap->ap_settings));
	wpa_printf(MSG_DEBUG, "WPS ER: Use cached AP settings");
	return 0;
}


static void wps_er_ap_remove_entry(struct wps_er *er, struct wps_er_ap *ap)
{
	wpa_printf(MSG_DEBUG, "WPS ER: Removing AP entry for %s (%s)",
		   inet_ntoa(ap->addr), ap->location);
	eloop_cancel_timeout(wps_er_ap_timeout, er, ap);
	wps_er_sta_remove_all(ap);
	wps_er_ap_event(er->wps, ap, WPS_EV_ER_AP_REMOVE);
	http_client_free(ap->http);
	ap->http = NULL;
	if (ap->wps) {
		wps_deinit(ap->wps);
		ap->wps = NULL;
	}

	dl_list_del(&ap->list);
	if (ap->subscribed) {
		dl_list_add(&er->ap_unsubscribing, &ap->list);
		wps_er_ap_unsubscribe(er, ap);
	} else
		wps_er_ap_free(ap);
}


static void wps_er_ap_timeout(void *eloop_data, void *user_ctx)
{
	struct wps_er *er = eloop_data;
	struct wps_er_ap *ap = user_ctx;
	wpa_printf(MSG_DEBUG, "WPS ER: AP advertisement timed out");
	wps_er_ap_remove_entry(er, ap);
}


static int wps_er_get_sid(struct wps_er_ap *ap, char *sid)
{
	char *pos;
	char txt[100];

	if (!sid) {
		wpa_printf(MSG_DEBUG, "WPS ER: No SID received from %s (%s)",
			   inet_ntoa(ap->addr), ap->location);
		return -1;
	}

	pos = os_strstr(sid, "uuid:");
	if (!pos) {
		wpa_printf(MSG_DEBUG, "WPS ER: Invalid SID received from "
			   "%s (%s): '%s'", inet_ntoa(ap->addr), ap->location,
			   sid);
		return -1;
	}

	pos += 5;
	if (uuid_str2bin(pos, ap->sid) < 0) {
		wpa_printf(MSG_DEBUG, "WPS ER: Invalid SID received from "
			   "%s (%s): '%s'", inet_ntoa(ap->addr), ap->location,
			   sid);
		return -1;
	}

	uuid_bin2str(ap->sid, txt, sizeof(txt));
	wpa_printf(MSG_DEBUG, "WPS ER: SID for subscription with %s (%s): %s",
		   inet_ntoa(ap->addr), ap->location, txt);

	return 0;
}


static void wps_er_http_subscribe_cb(void *ctx, struct http_client *c,
				     enum http_client_event event)
{
	struct wps_er_ap *ap = ctx;

	switch (event) {
	case HTTP_CLIENT_OK:
		wpa_printf(MSG_DEBUG, "WPS ER: Subscribed to events");
		ap->subscribed = 1;
		wps_er_get_sid(ap, http_client_get_hdr_line(c, "SID"));
		wps_er_ap_use_cached_settings(ap->er, ap);
		wps_er_ap_event(ap->er->wps, ap, WPS_EV_ER_AP_ADD);
		break;
	case HTTP_CLIENT_FAILED:
	case HTTP_CLIENT_INVALID_REPLY:
	case HTTP_CLIENT_TIMEOUT:
		wpa_printf(MSG_DEBUG, "WPS ER: Failed to subscribe to events");
		break;
	}
	http_client_free(ap->http);
	ap->http = NULL;
}


static void wps_er_subscribe(struct wps_er_ap *ap)
{
	struct wpabuf *req;
	struct sockaddr_in dst;
	char *url, *path;

	if (ap->event_sub_url == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: No eventSubURL - cannot "
			   "subscribe");
		return;
	}
	if (ap->http) {
		wpa_printf(MSG_DEBUG, "WPS ER: Pending HTTP request - cannot "
			   "send subscribe request");
		return;
	}

	url = http_client_url_parse(ap->event_sub_url, &dst, &path);
	if (url == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: Failed to parse eventSubURL");
		return;
	}

	req = wpabuf_alloc(os_strlen(ap->event_sub_url) + 1000);
	if (req == NULL) {
		os_free(url);
		return;
	}
	wpabuf_printf(req,
		      "SUBSCRIBE %s HTTP/1.1\r\n"
		      "HOST: %s:%d\r\n"
		      "CALLBACK: <http://%s:%d/event/%u/%u>\r\n"
		      "NT: upnp:event\r\n"
		      "TIMEOUT: Second-%d\r\n"
		      "\r\n",
		      path, inet_ntoa(dst.sin_addr), ntohs(dst.sin_port),
		      ap->er->ip_addr_text, ap->er->http_port,
		      ap->er->event_id, ap->id, 1800);
	os_free(url);
	wpa_hexdump_ascii(MSG_MSGDUMP, "WPS ER: Subscription request",
			  wpabuf_head(req), wpabuf_len(req));

	ap->http = http_client_addr(&dst, req, 1000, wps_er_http_subscribe_cb,
				    ap);
	if (ap->http == NULL)
		wpabuf_free(req);
}


static void wps_er_ap_get_m1(struct wps_er_ap *ap, struct wpabuf *m1)
{
	struct wps_parse_attr attr;

	if (wps_parse_msg(m1, &attr) < 0) {
		wpa_printf(MSG_DEBUG, "WPS ER: Failed to parse M1");
		return;
	}
	if (attr.primary_dev_type)
		os_memcpy(ap->pri_dev_type, attr.primary_dev_type, 8);
	if (attr.wps_state)
		ap->wps_state = *attr.wps_state;
	if (attr.mac_addr)
		os_memcpy(ap->mac_addr, attr.mac_addr, ETH_ALEN);

	wps_er_subscribe(ap);
}


static void wps_er_get_device_info(struct wps_er_ap *ap)
{
	wps_er_send_get_device_info(ap, wps_er_ap_get_m1);
}


static void wps_er_parse_device_description(struct wps_er_ap *ap,
					    struct wpabuf *reply)
{
	/* Note: reply includes null termination after the buffer data */
	const char *data = wpabuf_head(reply);
	char *pos;

	wpa_hexdump_ascii(MSG_MSGDUMP, "WPS ER: Device info",
			  wpabuf_head(reply), wpabuf_len(reply));

	ap->friendly_name = xml_get_first_item(data, "friendlyName");
	wpa_printf(MSG_DEBUG, "WPS ER: friendlyName='%s'", ap->friendly_name);

	ap->manufacturer = xml_get_first_item(data, "manufacturer");
	wpa_printf(MSG_DEBUG, "WPS ER: manufacturer='%s'", ap->manufacturer);

	ap->manufacturer_url = xml_get_first_item(data, "manufacturerURL");
	wpa_printf(MSG_DEBUG, "WPS ER: manufacturerURL='%s'",
		   ap->manufacturer_url);

	ap->model_description = xml_get_first_item(data, "modelDescription");
	wpa_printf(MSG_DEBUG, "WPS ER: modelDescription='%s'",
		   ap->model_description);

	ap->model_name = xml_get_first_item(data, "modelName");
	wpa_printf(MSG_DEBUG, "WPS ER: modelName='%s'", ap->model_name);

	ap->model_number = xml_get_first_item(data, "modelNumber");
	wpa_printf(MSG_DEBUG, "WPS ER: modelNumber='%s'", ap->model_number);

	ap->model_url = xml_get_first_item(data, "modelURL");
	wpa_printf(MSG_DEBUG, "WPS ER: modelURL='%s'", ap->model_url);

	ap->serial_number = xml_get_first_item(data, "serialNumber");
	wpa_printf(MSG_DEBUG, "WPS ER: serialNumber='%s'", ap->serial_number);

	ap->udn = xml_get_first_item(data, "UDN");
	wpa_printf(MSG_DEBUG, "WPS ER: UDN='%s'", ap->udn);
	pos = os_strstr(ap->udn, "uuid:");
	if (pos) {
		pos += 5;
		if (uuid_str2bin(pos, ap->uuid) < 0)
			wpa_printf(MSG_DEBUG, "WPS ER: Invalid UUID in UDN");
	}

	ap->upc = xml_get_first_item(data, "UPC");
	wpa_printf(MSG_DEBUG, "WPS ER: UPC='%s'", ap->upc);

	ap->scpd_url = http_link_update(
		xml_get_first_item(data, "SCPDURL"), ap->location);
	wpa_printf(MSG_DEBUG, "WPS ER: SCPDURL='%s'", ap->scpd_url);

	ap->control_url = http_link_update(
		xml_get_first_item(data, "controlURL"), ap->location);
	wpa_printf(MSG_DEBUG, "WPS ER: controlURL='%s'", ap->control_url);

	ap->event_sub_url = http_link_update(
		xml_get_first_item(data, "eventSubURL"), ap->location);
	wpa_printf(MSG_DEBUG, "WPS ER: eventSubURL='%s'", ap->event_sub_url);
}


static void wps_er_http_dev_desc_cb(void *ctx, struct http_client *c,
				    enum http_client_event event)
{
	struct wps_er_ap *ap = ctx;
	struct wpabuf *reply;
	int ok = 0;

	switch (event) {
	case HTTP_CLIENT_OK:
		reply = http_client_get_body(c);
		if (reply == NULL)
			break;
		wps_er_parse_device_description(ap, reply);
		ok = 1;
		break;
	case HTTP_CLIENT_FAILED:
	case HTTP_CLIENT_INVALID_REPLY:
	case HTTP_CLIENT_TIMEOUT:
		wpa_printf(MSG_DEBUG, "WPS ER: Failed to fetch device info");
		break;
	}
	http_client_free(ap->http);
	ap->http = NULL;
	if (ok)
		wps_er_get_device_info(ap);
}


void wps_er_ap_add(struct wps_er *er, const u8 *uuid, struct in_addr *addr,
		   const char *location, int max_age)
{
	struct wps_er_ap *ap;

	ap = wps_er_ap_get(er, addr, uuid);
	if (ap) {
		/* Update advertisement timeout */
		eloop_cancel_timeout(wps_er_ap_timeout, er, ap);
		eloop_register_timeout(max_age, 0, wps_er_ap_timeout, er, ap);
		return;
	}

	ap = os_zalloc(sizeof(*ap));
	if (ap == NULL)
		return;
	dl_list_init(&ap->sta);
	ap->er = er;
	ap->id = ++er->next_ap_id;
	ap->location = os_strdup(location);
	if (ap->location == NULL) {
		os_free(ap);
		return;
	}
	dl_list_add(&er->ap, &ap->list);

	ap->addr.s_addr = addr->s_addr;
	os_memcpy(ap->uuid, uuid, WPS_UUID_LEN);
	eloop_register_timeout(max_age, 0, wps_er_ap_timeout, er, ap);

	wpa_printf(MSG_DEBUG, "WPS ER: Added AP entry for %s (%s)",
		   inet_ntoa(ap->addr), ap->location);

	/* Fetch device description */
	ap->http = http_client_url(ap->location, NULL, 10000,
				   wps_er_http_dev_desc_cb, ap);
}


void wps_er_ap_remove(struct wps_er *er, struct in_addr *addr)
{
	struct wps_er_ap *ap;
	dl_list_for_each(ap, &er->ap, struct wps_er_ap, list) {
		if (ap->addr.s_addr == addr->s_addr) {
			wps_er_ap_remove_entry(er, ap);
			return;
		}
	}
}


static void wps_er_ap_remove_all(struct wps_er *er)
{
	struct wps_er_ap *prev, *ap;
	struct wps_er_ap_settings *prev_s, *s;
	dl_list_for_each_safe(ap, prev, &er->ap, struct wps_er_ap, list)
		wps_er_ap_remove_entry(er, ap);
	dl_list_for_each_safe(s, prev_s, &er->ap_settings,
			      struct wps_er_ap_settings, list)
		os_free(s);
}


static void http_put_date(struct wpabuf *buf)
{
	wpabuf_put_str(buf, "Date: ");
	format_date(buf);
	wpabuf_put_str(buf, "\r\n");
}


static void wps_er_http_resp_not_found(struct http_request *req)
{
	struct wpabuf *buf;
	buf = wpabuf_alloc(200);
	if (buf == NULL) {
		http_request_deinit(req);
		return;
	}

	wpabuf_put_str(buf,
		       "HTTP/1.1 404 Not Found\r\n"
		       "Server: unspecified, UPnP/1.0, unspecified\r\n"
		       "Connection: close\r\n");
	http_put_date(buf);
	wpabuf_put_str(buf, "\r\n");
	http_request_send_and_deinit(req, buf);
}


static void wps_er_http_resp_ok(struct http_request *req)
{
	struct wpabuf *buf;
	buf = wpabuf_alloc(200);
	if (buf == NULL) {
		http_request_deinit(req);
		return;
	}

	wpabuf_put_str(buf,
		       "HTTP/1.1 200 OK\r\n"
		       "Server: unspecified, UPnP/1.0, unspecified\r\n"
		       "Connection: close\r\n"
		       "Content-Length: 0\r\n");
	http_put_date(buf);
	wpabuf_put_str(buf, "\r\n");
	http_request_send_and_deinit(req, buf);
}


static void wps_er_sta_timeout(void *eloop_data, void *user_ctx)
{
	struct wps_er_sta *sta = eloop_data;
	wpa_printf(MSG_DEBUG, "WPS ER: STA entry timed out");
	dl_list_del(&sta->list);
	wps_er_sta_free(sta);
}


static struct wps_er_sta * wps_er_add_sta_data(struct wps_er_ap *ap,
					       const u8 *addr,
					       struct wps_parse_attr *attr,
					       int probe_req)
{
	struct wps_er_sta *sta = wps_er_sta_get(ap, addr, NULL);
	int new_sta = 0;
	int m1;

	m1 = !probe_req && attr->msg_type && *attr->msg_type == WPS_M1;

	if (sta == NULL) {
		/*
		 * Only allow new STA entry to be added based on Probe Request
		 * or M1. This will filter out bogus events and anything that
		 * may have been ongoing at the time ER subscribed for events.
		 */
		if (!probe_req && !m1)
			return NULL;

		sta = os_zalloc(sizeof(*sta));
		if (sta == NULL)
			return NULL;
		os_memcpy(sta->addr, addr, ETH_ALEN);
		sta->ap = ap;
		dl_list_add(&ap->sta, &sta->list);
		new_sta = 1;
	}

	if (m1)
		sta->m1_received = 1;

	if (attr->config_methods && (!probe_req || !sta->m1_received))
		sta->config_methods = WPA_GET_BE16(attr->config_methods);
	if (attr->uuid_e && (!probe_req || !sta->m1_received))
		os_memcpy(sta->uuid, attr->uuid_e, WPS_UUID_LEN);
	if (attr->primary_dev_type && (!probe_req || !sta->m1_received))
		os_memcpy(sta->pri_dev_type, attr->primary_dev_type, 8);
	if (attr->dev_password_id && (!probe_req || !sta->m1_received))
		sta->dev_passwd_id = WPA_GET_BE16(attr->dev_password_id);

	if (attr->manufacturer) {
		os_free(sta->manufacturer);
		sta->manufacturer = os_malloc(attr->manufacturer_len + 1);
		if (sta->manufacturer) {
			os_memcpy(sta->manufacturer, attr->manufacturer,
				  attr->manufacturer_len);
			sta->manufacturer[attr->manufacturer_len] = '\0';
		}
	}

	if (attr->model_name) {
		os_free(sta->model_name);
		sta->model_name = os_malloc(attr->model_name_len + 1);
		if (sta->model_name) {
			os_memcpy(sta->model_name, attr->model_name,
				  attr->model_name_len);
			sta->model_name[attr->model_name_len] = '\0';
		}
	}

	if (attr->model_number) {
		os_free(sta->model_number);
		sta->model_number = os_malloc(attr->model_number_len + 1);
		if (sta->model_number) {
			os_memcpy(sta->model_number, attr->model_number,
				  attr->model_number_len);
			sta->model_number[attr->model_number_len] = '\0';
		}
	}

	if (attr->serial_number) {
		os_free(sta->serial_number);
		sta->serial_number = os_malloc(attr->serial_number_len + 1);
		if (sta->serial_number) {
			os_memcpy(sta->serial_number, attr->serial_number,
				  attr->serial_number_len);
			sta->serial_number[attr->serial_number_len] = '\0';
		}
	}

	if (attr->dev_name) {
		os_free(sta->dev_name);
		sta->dev_name = os_malloc(attr->dev_name_len + 1);
		if (sta->dev_name) {
			os_memcpy(sta->dev_name, attr->dev_name,
				  attr->dev_name_len);
			sta->dev_name[attr->dev_name_len] = '\0';
		}
	}

	eloop_cancel_timeout(wps_er_sta_timeout, sta, NULL);
	eloop_register_timeout(300, 0, wps_er_sta_timeout, sta, NULL);

	if (m1 || new_sta)
		wps_er_sta_event(ap->er->wps, sta, WPS_EV_ER_ENROLLEE_ADD);

	return sta;
}


static void wps_er_process_wlanevent_probe_req(struct wps_er_ap *ap,
					       const u8 *addr,
					       struct wpabuf *msg)
{
	struct wps_parse_attr attr;

	wpa_printf(MSG_DEBUG, "WPS ER: WLANEvent - Probe Request - from "
		   MACSTR, MAC2STR(addr));
	wpa_hexdump_buf(MSG_MSGDUMP, "WPS ER: WLANEvent - Enrollee's message "
			"(TLVs from Probe Request)", msg);

	if (wps_validate_probe_req(msg, addr) < 0) {
		wpa_printf(MSG_INFO, "WPS-STRICT: ER: Ignore invalid proxied "
			   "Probe Request frame from " MACSTR, MAC2STR(addr));
		return;
	}

	if (wps_parse_msg(msg, &attr) < 0) {
		wpa_printf(MSG_DEBUG, "WPS ER: Failed to parse TLVs in "
			   "WLANEvent message");
		return;
	}

	wps_er_add_sta_data(ap, addr, &attr, 1);
	wps_registrar_probe_req_rx(ap->er->wps->registrar, addr, msg, 0);
}


static void wps_er_http_put_wlan_response_cb(void *ctx, struct http_client *c,
					     enum http_client_event event)
{
	struct wps_er_sta *sta = ctx;

	switch (event) {
	case HTTP_CLIENT_OK:
		wpa_printf(MSG_DEBUG, "WPS ER: PutWLANResponse OK");
		break;
	case HTTP_CLIENT_FAILED:
	case HTTP_CLIENT_INVALID_REPLY:
	case HTTP_CLIENT_TIMEOUT:
		wpa_printf(MSG_DEBUG, "WPS ER: PutWLANResponse failed");
		break;
	}
	http_client_free(sta->http);
	sta->http = NULL;
}


static const char *soap_prefix =
	"<?xml version=\"1.0\"?>\n"
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\n"
	"<s:Body>\n";
static const char *soap_postfix =
	"</s:Body>\n</s:Envelope>\n";
static const char *urn_wfawlanconfig =
	"urn:schemas-wifialliance-org:service:WFAWLANConfig:1";

static struct wpabuf * wps_er_soap_hdr(const struct wpabuf *msg,
				       const char *name, const char *arg_name,
				       const char *path,
				       const struct sockaddr_in *dst,
				       char **len_ptr, char **body_ptr)
{
	unsigned char *encoded;
	size_t encoded_len;
	struct wpabuf *buf;

	if (msg) {
		encoded = base64_encode(wpabuf_head(msg), wpabuf_len(msg),
					&encoded_len);
		if (encoded == NULL)
			return NULL;
	} else {
		encoded = NULL;
		encoded_len = 0;
	}

	buf = wpabuf_alloc(1000 + encoded_len);
	if (buf == NULL) {
		os_free(encoded);
		return NULL;
	}

	wpabuf_printf(buf,
		      "POST %s HTTP/1.1\r\n"
		      "Host: %s:%d\r\n"
		      "Content-Type: text/xml; charset=\"utf-8\"\r\n"
		      "Content-Length: ",
		      path, inet_ntoa(dst->sin_addr), ntohs(dst->sin_port));

	*len_ptr = wpabuf_put(buf, 0);
	wpabuf_printf(buf,
		      "        \r\n"
		      "SOAPACTION: \"%s#%s\"\r\n"
		      "\r\n",
		      urn_wfawlanconfig, name);

	*body_ptr = wpabuf_put(buf, 0);

	wpabuf_put_str(buf, soap_prefix);
	wpabuf_printf(buf, "<u:%s xmlns:u=\"", name);
	wpabuf_put_str(buf, urn_wfawlanconfig);
	wpabuf_put_str(buf, "\">\n");
	if (encoded) {
		wpabuf_printf(buf, "<%s>%s</%s>\n",
			      arg_name, (char *) encoded, arg_name);
		os_free(encoded);
	}

	return buf;
}


static void wps_er_soap_end(struct wpabuf *buf, const char *name,
			    char *len_ptr, char *body_ptr)
{
	char len_buf[10];
	wpabuf_printf(buf, "</u:%s>\n", name);
	wpabuf_put_str(buf, soap_postfix);
	os_snprintf(len_buf, sizeof(len_buf), "%d",
		    (int) ((char *) wpabuf_put(buf, 0) - body_ptr));
	os_memcpy(len_ptr, len_buf, os_strlen(len_buf));
}


static void wps_er_sta_send_msg(struct wps_er_sta *sta, struct wpabuf *msg)
{
	struct wpabuf *buf;
	char *len_ptr, *body_ptr;
	struct sockaddr_in dst;
	char *url, *path;

	if (sta->http) {
		wpa_printf(MSG_DEBUG, "WPS ER: Pending HTTP request for STA - "
			   "ignore new request");
		wpabuf_free(msg);
		return;
	}

	if (sta->ap->control_url == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: No controlURL for AP");
		wpabuf_free(msg);
		return;
	}

	url = http_client_url_parse(sta->ap->control_url, &dst, &path);
	if (url == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: Failed to parse controlURL");
		wpabuf_free(msg);
		return;
	}

	buf = wps_er_soap_hdr(msg, "PutWLANResponse", "NewMessage", path, &dst,
			      &len_ptr, &body_ptr);
	wpabuf_free(msg);
	os_free(url);
	if (buf == NULL)
		return;
	wpabuf_printf(buf, "<NewWLANEventType>%d</NewWLANEventType>\n",
		      UPNP_WPS_WLANEVENT_TYPE_EAP);
	wpabuf_printf(buf, "<NewWLANEventMAC>" MACSTR "</NewWLANEventMAC>\n",
		      MAC2STR(sta->addr));

	wps_er_soap_end(buf, "PutWLANResponse", len_ptr, body_ptr);

	sta->http = http_client_addr(&dst, buf, 1000,
				     wps_er_http_put_wlan_response_cb, sta);
	if (sta->http == NULL)
		wpabuf_free(buf);
}


static void wps_er_sta_process(struct wps_er_sta *sta, struct wpabuf *msg,
			       enum wsc_op_code op_code)
{
	enum wps_process_res res;

	res = wps_process_msg(sta->wps, op_code, msg);
	if (res == WPS_CONTINUE) {
		struct wpabuf *next = wps_get_msg(sta->wps, &op_code);
		if (next)
			wps_er_sta_send_msg(sta, next);
	} else {
		wpa_printf(MSG_DEBUG, "WPS ER: Protocol run %s with the "
			   "enrollee (res=%d)",
			   res == WPS_DONE ? "succeeded" : "failed", res);
		wps_deinit(sta->wps);
		sta->wps = NULL;
		if (res == WPS_DONE) {
			/* Remove the STA entry after short timeout */
			eloop_cancel_timeout(wps_er_sta_timeout, sta, NULL);
			eloop_register_timeout(10, 0, wps_er_sta_timeout, sta,
					       NULL);
		}
	}
}


static void wps_er_sta_start(struct wps_er_sta *sta, struct wpabuf *msg)
{
	struct wps_config cfg;

	if (sta->wps)
		wps_deinit(sta->wps);

	os_memset(&cfg, 0, sizeof(cfg));
	cfg.wps = sta->ap->er->wps;
	cfg.registrar = 1;
	cfg.peer_addr = sta->addr;

	sta->wps = wps_init(&cfg);
	if (sta->wps == NULL)
		return;
	sta->wps->er = 1;
	sta->wps->use_cred = sta->ap->ap_settings;
	if (sta->ap->ap_settings) {
		os_free(sta->cred);
		sta->cred = os_malloc(sizeof(*sta->cred));
		if (sta->cred) {
			os_memcpy(sta->cred, sta->ap->ap_settings,
				  sizeof(*sta->cred));
			sta->cred->cred_attr = NULL;
			os_memcpy(sta->cred->mac_addr, sta->addr, ETH_ALEN);
			sta->wps->use_cred = sta->cred;
		}
	}

	wps_er_sta_process(sta, msg, WSC_MSG);
}


static void wps_er_process_wlanevent_eap(struct wps_er_ap *ap, const u8 *addr,
					 struct wpabuf *msg)
{
	struct wps_parse_attr attr;
	struct wps_er_sta *sta;

	wpa_printf(MSG_DEBUG, "WPS ER: WLANEvent - EAP - from " MACSTR,
		   MAC2STR(addr));
	wpa_hexdump_buf(MSG_MSGDUMP, "WPS ER: WLANEvent - Enrollee's message "
			"(TLVs from EAP-WSC)", msg);

	if (wps_parse_msg(msg, &attr) < 0) {
		wpa_printf(MSG_DEBUG, "WPS ER: Failed to parse TLVs in "
			   "WLANEvent message");
		return;
	}

	sta = wps_er_add_sta_data(ap, addr, &attr, 0);
	if (sta == NULL)
		return;

	if (attr.msg_type && *attr.msg_type == WPS_M1)
		wps_er_sta_start(sta, msg);
	else if (sta->wps) {
		enum wsc_op_code op_code = WSC_MSG;
		if (attr.msg_type) {
			switch (*attr.msg_type) {
			case WPS_WSC_ACK:
				op_code = WSC_ACK;
				break;
			case WPS_WSC_NACK:
				op_code = WSC_NACK;
				break;
			case WPS_WSC_DONE:
				op_code = WSC_Done;
				break;
			}
		}
		wps_er_sta_process(sta, msg, op_code);
	}
}


static void wps_er_process_wlanevent(struct wps_er_ap *ap,
				     struct wpabuf *event)
{
	u8 *data;
	u8 wlan_event_type;
	u8 wlan_event_mac[ETH_ALEN];
	struct wpabuf msg;

	wpa_hexdump(MSG_MSGDUMP, "WPS ER: Received WLANEvent",
		    wpabuf_head(event), wpabuf_len(event));
	if (wpabuf_len(event) < 1 + 17) {
		wpa_printf(MSG_DEBUG, "WPS ER: Too short WLANEvent");
		return;
	}

	data = wpabuf_mhead(event);
	wlan_event_type = data[0];
	if (hwaddr_aton((char *) data + 1, wlan_event_mac) < 0) {
		wpa_printf(MSG_DEBUG, "WPS ER: Invalid WLANEventMAC in "
			   "WLANEvent");
		return;
	}

	wpabuf_set(&msg, data + 1 + 17, wpabuf_len(event) - (1 + 17));

	switch (wlan_event_type) {
	case 1:
		wps_er_process_wlanevent_probe_req(ap, wlan_event_mac, &msg);
		break;
	case 2:
		wps_er_process_wlanevent_eap(ap, wlan_event_mac, &msg);
		break;
	default:
		wpa_printf(MSG_DEBUG, "WPS ER: Unknown WLANEventType %d",
			   wlan_event_type);
		break;
	}
}


static void wps_er_http_event(struct wps_er *er, struct http_request *req,
			      unsigned int ap_id)
{
	struct wps_er_ap *ap = wps_er_ap_get_id(er, ap_id);
	struct wpabuf *event;
	enum http_reply_code ret;

	if (ap == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: HTTP event from unknown AP id "
			   "%u", ap_id);
		wps_er_http_resp_not_found(req);
		return;
	}
	wpa_printf(MSG_MSGDUMP, "WPS ER: HTTP event from AP id %u: %s",
		   ap_id, http_request_get_data(req));

	event = xml_get_base64_item(http_request_get_data(req), "WLANEvent",
				    &ret);
	if (event == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: Could not extract WLANEvent "
			   "from the event notification");
		/*
		 * Reply with OK anyway to avoid getting unregistered from
		 * events.
		 */
		wps_er_http_resp_ok(req);
		return;
	}

	wps_er_process_wlanevent(ap, event);

	wpabuf_free(event);
	wps_er_http_resp_ok(req);
}


static void wps_er_http_notify(struct wps_er *er, struct http_request *req)
{
	char *uri = http_request_get_uri(req);

	if (os_strncmp(uri, "/event/", 7) == 0) {
		unsigned int event_id;
		char *pos;
		event_id = atoi(uri + 7);
		if (event_id != er->event_id) {
			wpa_printf(MSG_DEBUG, "WPS ER: HTTP event for an "
				   "unknown event id %u", event_id);
			return;
		}
		pos = os_strchr(uri + 7, '/');
		if (pos == NULL)
			return;
		pos++;
		wps_er_http_event(er, req, atoi(pos));
	} else {
		wpa_printf(MSG_DEBUG, "WPS ER: Unknown HTTP NOTIFY for '%s'",
			   uri);
		wps_er_http_resp_not_found(req);
	}
}


static void wps_er_http_req(void *ctx, struct http_request *req)
{
	struct wps_er *er = ctx;
	struct sockaddr_in *cli = http_request_get_cli_addr(req);
	enum httpread_hdr_type type = http_request_get_type(req);
	struct wpabuf *buf;

	wpa_printf(MSG_DEBUG, "WPS ER: HTTP request: '%s' (type %d) from "
		   "%s:%d",
		   http_request_get_uri(req), type,
		   inet_ntoa(cli->sin_addr), ntohs(cli->sin_port));

	switch (type) {
	case HTTPREAD_HDR_TYPE_NOTIFY:
		wps_er_http_notify(er, req);
		break;
	default:
		wpa_printf(MSG_DEBUG, "WPS ER: Unsupported HTTP request type "
			   "%d", type);
		buf = wpabuf_alloc(200);
		if (buf == NULL) {
			http_request_deinit(req);
			return;
		}
		wpabuf_put_str(buf,
			       "HTTP/1.1 501 Unimplemented\r\n"
			       "Connection: close\r\n");
		http_put_date(buf);
		wpabuf_put_str(buf, "\r\n");
		http_request_send_and_deinit(req, buf);
		break;
	}
}


struct wps_er *
wps_er_init(struct wps_context *wps, const char *ifname, const char *filter)
{
	struct wps_er *er;
	struct in_addr addr;

	er = os_zalloc(sizeof(*er));
	if (er == NULL)
		return NULL;
	dl_list_init(&er->ap);
	dl_list_init(&er->ap_unsubscribing);
	dl_list_init(&er->ap_settings);

	er->multicast_sd = -1;
	er->ssdp_sd = -1;

	os_strlcpy(er->ifname, ifname, sizeof(er->ifname));
	er->wps = wps;
	if (os_get_random((unsigned char *) &er->event_id,
			  sizeof(er->event_id)) < 0) {
		wps_er_deinit(er, NULL, NULL);
		return NULL;
	}
	/* Limit event_id to < 32 bits to avoid issues with atoi() */
	er->event_id &= 0x0fffffff;

	if (filter) {
		if (inet_aton(filter, &er->filter_addr) == 0) {
			wpa_printf(MSG_INFO, "WPS UPnP: Invalid filter "
				   "address %s", filter);
			wps_er_deinit(er, NULL, NULL);
			return NULL;
		}
		wpa_printf(MSG_DEBUG, "WPS UPnP: Only accepting connections "
			   "with %s", filter);
	}
	if (get_netif_info(ifname, &er->ip_addr, &er->ip_addr_text,
			   er->mac_addr)) {
		wpa_printf(MSG_INFO, "WPS UPnP: Could not get IP/MAC address "
			   "for %s. Does it have IP address?", ifname);
		wps_er_deinit(er, NULL, NULL);
		return NULL;
	}

	if (wps_er_ssdp_init(er) < 0) {
		wpa_printf(MSG_INFO, "WPS UPnP: SSDP initialization failed");
		wps_er_deinit(er, NULL, NULL);
		return NULL;
	}

	addr.s_addr = er->ip_addr;
	er->http_srv = http_server_init(&addr, -1, wps_er_http_req, er);
	if (er->http_srv == NULL) {
		wpa_printf(MSG_INFO, "WPS UPnP: HTTP initialization failed");
		wps_er_deinit(er, NULL, NULL);
		return NULL;
	}
	er->http_port = http_server_get_port(er->http_srv);

	wpa_printf(MSG_DEBUG, "WPS ER: Start (ifname=%s ip_addr=%s)",
		   er->ifname, er->ip_addr_text);

	return er;
}


void wps_er_refresh(struct wps_er *er)
{
	struct wps_er_ap *ap;
	struct wps_er_sta *sta;

	dl_list_for_each(ap, &er->ap, struct wps_er_ap, list) {
		wps_er_ap_event(er->wps, ap, WPS_EV_ER_AP_ADD);
		dl_list_for_each(sta, &ap->sta, struct wps_er_sta, list)
			wps_er_sta_event(er->wps, sta, WPS_EV_ER_ENROLLEE_ADD);
	}

	wps_er_send_ssdp_msearch(er);
}


static void wps_er_deinit_finish(void *eloop_data, void *user_ctx)
{
	struct wps_er *er = eloop_data;
	void (*deinit_done_cb)(void *ctx);
	void *deinit_done_ctx;

	wpa_printf(MSG_DEBUG, "WPS ER: Finishing deinit");

	deinit_done_cb = er->deinit_done_cb;
	deinit_done_ctx = er->deinit_done_ctx;
	os_free(er->ip_addr_text);
	os_free(er);

	if (deinit_done_cb)
		deinit_done_cb(deinit_done_ctx);
}


void wps_er_deinit(struct wps_er *er, void (*cb)(void *ctx), void *ctx)
{
	if (er == NULL)
		return;
	http_server_deinit(er->http_srv);
	wps_er_ap_remove_all(er);
	wps_er_ssdp_deinit(er);
	eloop_register_timeout(dl_list_empty(&er->ap_unsubscribing) ? 0 : 5, 0,
			       wps_er_deinit_finish, er, NULL);
	wpa_printf(MSG_DEBUG, "WPS ER: Finish deinit from timeout");
	er->deinitializing = 1;
	er->deinit_done_cb = cb;
	er->deinit_done_ctx = ctx;
}


static void wps_er_http_set_sel_reg_cb(void *ctx, struct http_client *c,
				       enum http_client_event event)
{
	struct wps_er_ap *ap = ctx;
	union wps_event_data data;

	os_memset(&data, 0, sizeof(data));

	switch (event) {
	case HTTP_CLIENT_OK:
		wpa_printf(MSG_DEBUG, "WPS ER: SetSelectedRegistrar OK");
		data.set_sel_reg.state = WPS_ER_SET_SEL_REG_DONE;
		data.set_sel_reg.uuid = ap->uuid;
		break;
	case HTTP_CLIENT_FAILED:
	case HTTP_CLIENT_INVALID_REPLY:
	case HTTP_CLIENT_TIMEOUT:
		wpa_printf(MSG_DEBUG, "WPS ER: SetSelectedRegistrar failed");
		data.set_sel_reg.state = WPS_ER_SET_SEL_REG_FAILED;
		data.set_sel_reg.uuid = ap->uuid;
		break;
	}
	http_client_free(ap->http);
	ap->http = NULL;

	if (data.set_sel_reg.uuid)
		ap->er->wps->event_cb(ap->er->wps->cb_ctx,
				      WPS_EV_ER_SET_SELECTED_REGISTRAR, &data);
}


static void wps_er_send_set_sel_reg(struct wps_er_ap *ap, struct wpabuf *msg)
{
	struct wpabuf *buf;
	char *len_ptr, *body_ptr;
	struct sockaddr_in dst;
	char *url, *path;

	if (ap->control_url == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: No controlURL for AP");
		return;
	}

	if (ap->http) {
		wpa_printf(MSG_DEBUG, "WPS ER: Pending HTTP request for AP - "
			   "ignore new request");
		return;
	}

	if (ap->wps) {
		wpa_printf(MSG_DEBUG, "WPS ER: Pending WPS operation for AP - "
			   "skip SetSelectedRegistrar");
		return;
	}

	url = http_client_url_parse(ap->control_url, &dst, &path);
	if (url == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: Failed to parse controlURL");
		return;
	}

	buf = wps_er_soap_hdr(msg, "SetSelectedRegistrar", "NewMessage", path,
			      &dst, &len_ptr, &body_ptr);
	os_free(url);
	if (buf == NULL)
		return;

	wps_er_soap_end(buf, "SetSelectedRegistrar", len_ptr, body_ptr);

	ap->http = http_client_addr(&dst, buf, 1000,
				    wps_er_http_set_sel_reg_cb, ap);
	if (ap->http == NULL)
		wpabuf_free(buf);
}


static int wps_er_build_selected_registrar(struct wpabuf *msg, int sel_reg)
{
	wpabuf_put_be16(msg, ATTR_SELECTED_REGISTRAR);
	wpabuf_put_be16(msg, 1);
	wpabuf_put_u8(msg, !!sel_reg);
	return 0;
}


static int wps_er_build_dev_password_id(struct wpabuf *msg, u16 dev_passwd_id)
{
	wpabuf_put_be16(msg, ATTR_DEV_PASSWORD_ID);
	wpabuf_put_be16(msg, 2);
	wpabuf_put_be16(msg, dev_passwd_id);
	return 0;
}


static int wps_er_build_sel_reg_config_methods(struct wpabuf *msg,
					       u16 sel_reg_config_methods)
{
	wpabuf_put_be16(msg, ATTR_SELECTED_REGISTRAR_CONFIG_METHODS);
	wpabuf_put_be16(msg, 2);
	wpabuf_put_be16(msg, sel_reg_config_methods);
	return 0;
}


static int wps_er_build_uuid_r(struct wpabuf *msg, const u8 *uuid_r)
{
#ifdef CONFIG_WPS2
	wpabuf_put_be16(msg, ATTR_UUID_R);
	wpabuf_put_be16(msg, WPS_UUID_LEN);
	wpabuf_put_data(msg, uuid_r, WPS_UUID_LEN);
#endif /* CONFIG_WPS2 */
	return 0;
}


void wps_er_set_sel_reg(struct wps_er *er, int sel_reg, u16 dev_passwd_id,
			u16 sel_reg_config_methods)
{
	struct wpabuf *msg;
	struct wps_er_ap *ap;
	struct wps_registrar *reg = er->wps->registrar;
	const u8 *auth_macs;
#ifdef CONFIG_WPS2
	u8 bcast[ETH_ALEN];
#endif /* CONFIG_WPS2 */
	size_t count;
	union wps_event_data data;

	if (er->skip_set_sel_reg) {
		wpa_printf(MSG_DEBUG, "WPS ER: Skip SetSelectedRegistrar");
		return;
	}

	msg = wpabuf_alloc(500);
	if (msg == NULL)
		return;

	auth_macs = wps_authorized_macs(reg, &count);
#ifdef CONFIG_WPS2
	if (count == 0) {
		os_memset(bcast, 0xff, ETH_ALEN);
		auth_macs = bcast;
		count = 1;
	}
#endif /* CONFIG_WPS2 */

	if (wps_build_version(msg) ||
	    wps_er_build_selected_registrar(msg, sel_reg) ||
	    wps_er_build_dev_password_id(msg, dev_passwd_id) ||
	    wps_er_build_sel_reg_config_methods(msg, sel_reg_config_methods) ||
	    wps_build_wfa_ext(msg, 0, auth_macs, count) ||
	    wps_er_build_uuid_r(msg, er->wps->uuid)) {
		wpabuf_free(msg);
		return;
	}

	os_memset(&data, 0, sizeof(data));
	data.set_sel_reg.sel_reg = sel_reg;
	data.set_sel_reg.dev_passwd_id = dev_passwd_id;
	data.set_sel_reg.sel_reg_config_methods = sel_reg_config_methods;
	data.set_sel_reg.state = WPS_ER_SET_SEL_REG_START;

	dl_list_for_each(ap, &er->ap, struct wps_er_ap, list) {
		if (er->set_sel_reg_uuid_filter &&
		    os_memcmp(ap->uuid, er->set_sel_reg_uuid_filter,
			      WPS_UUID_LEN) != 0)
			continue;
		data.set_sel_reg.uuid = ap->uuid;
		er->wps->event_cb(er->wps->cb_ctx,
				  WPS_EV_ER_SET_SELECTED_REGISTRAR, &data);
		wps_er_send_set_sel_reg(ap, msg);
	}

	wpabuf_free(msg);
}


int wps_er_pbc(struct wps_er *er, const u8 *uuid)
{
	int res;
	struct wps_er_ap *ap;

	if (er == NULL || er->wps == NULL)
		return -1;

	if (wps_registrar_pbc_overlap(er->wps->registrar, NULL, NULL)) {
		wpa_printf(MSG_DEBUG, "WPS ER: PBC overlap - do not start PBC "
			   "mode");
		return -2;
	}

	ap = wps_er_ap_get(er, NULL, uuid);
	if (ap == NULL) {
		struct wps_er_sta *sta = NULL;
		dl_list_for_each(ap, &er->ap, struct wps_er_ap, list) {
			sta = wps_er_sta_get(ap, NULL, uuid);
			if (sta) {
				uuid = ap->uuid;
				break;
			}
		}
		if (sta == NULL)
			return -3; /* Unknown UUID */
	}

	if (ap->ap_settings == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: AP settings not known");
		return -4;
	}

	er->set_sel_reg_uuid_filter = uuid;
	res = wps_registrar_button_pushed(er->wps->registrar, NULL);
	er->set_sel_reg_uuid_filter = NULL;
	if (res)
		return -1;

	return 0;
}


static void wps_er_ap_settings_cb(void *ctx, const struct wps_credential *cred)
{
	struct wps_er_ap *ap = ctx;
	union wps_event_data data;

	wpa_printf(MSG_DEBUG, "WPS ER: AP Settings received");
	os_free(ap->ap_settings);
	ap->ap_settings = os_malloc(sizeof(*cred));
	if (ap->ap_settings) {
		os_memcpy(ap->ap_settings, cred, sizeof(*cred));
		ap->ap_settings->cred_attr = NULL;
	}

	os_memset(&data, 0, sizeof(data));
	data.ap_settings.uuid = ap->uuid;
	data.ap_settings.cred = cred;
	ap->er->wps->event_cb(ap->er->wps->cb_ctx, WPS_EV_ER_AP_SETTINGS,
			      &data);
}


static void wps_er_http_put_message_cb(void *ctx, struct http_client *c,
				       enum http_client_event event)
{
	struct wps_er_ap *ap = ctx;
	struct wpabuf *reply;
	char *msg = NULL;

	switch (event) {
	case HTTP_CLIENT_OK:
		wpa_printf(MSG_DEBUG, "WPS ER: PutMessage OK");
		reply = http_client_get_body(c);
		if (reply == NULL)
			break;
		msg = os_zalloc(wpabuf_len(reply) + 1);
		if (msg == NULL)
			break;
		os_memcpy(msg, wpabuf_head(reply), wpabuf_len(reply));
		break;
	case HTTP_CLIENT_FAILED:
	case HTTP_CLIENT_INVALID_REPLY:
	case HTTP_CLIENT_TIMEOUT:
		wpa_printf(MSG_DEBUG, "WPS ER: PutMessage failed");
		if (ap->wps) {
			wps_deinit(ap->wps);
			ap->wps = NULL;
		}
		break;
	}
	http_client_free(ap->http);
	ap->http = NULL;

	if (msg) {
		struct wpabuf *buf;
		enum http_reply_code ret;
		buf = xml_get_base64_item(msg, "NewOutMessage", &ret);
		os_free(msg);
		if (buf == NULL) {
			wpa_printf(MSG_DEBUG, "WPS ER: Could not extract "
				   "NewOutMessage from PutMessage response");
			wps_deinit(ap->wps);
			ap->wps = NULL;
			return;
		}
		wps_er_ap_process(ap, buf);
		wpabuf_free(buf);
	}
}


static void wps_er_ap_put_message(struct wps_er_ap *ap,
				  const struct wpabuf *msg)
{
	struct wpabuf *buf;
	char *len_ptr, *body_ptr;
	struct sockaddr_in dst;
	char *url, *path;

	if (ap->http) {
		wpa_printf(MSG_DEBUG, "WPS ER: Pending HTTP operation ongoing "
			   "with the AP - cannot continue learn");
		return;
	}

	if (ap->control_url == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: No controlURL for AP");
		return;
	}

	url = http_client_url_parse(ap->control_url, &dst, &path);
	if (url == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: Failed to parse controlURL");
		return;
	}

	buf = wps_er_soap_hdr(msg, "PutMessage", "NewInMessage", path, &dst,
			      &len_ptr, &body_ptr);
	os_free(url);
	if (buf == NULL)
		return;

	wps_er_soap_end(buf, "PutMessage", len_ptr, body_ptr);

	ap->http = http_client_addr(&dst, buf, 10000,
				    wps_er_http_put_message_cb, ap);
	if (ap->http == NULL)
		wpabuf_free(buf);
}


static void wps_er_ap_process(struct wps_er_ap *ap, struct wpabuf *msg)
{
	enum wps_process_res res;
	struct wps_parse_attr attr;
	enum wsc_op_code op_code;

	op_code = WSC_MSG;
	if (wps_parse_msg(msg, &attr) == 0 && attr.msg_type) {
		switch (*attr.msg_type) {
		case WPS_WSC_ACK:
			op_code = WSC_ACK;
			break;
		case WPS_WSC_NACK:
			op_code = WSC_NACK;
			break;
		case WPS_WSC_DONE:
			op_code = WSC_Done;
			break;
		}
	}

	res = wps_process_msg(ap->wps, op_code, msg);
	if (res == WPS_CONTINUE) {
		struct wpabuf *next = wps_get_msg(ap->wps, &op_code);
		if (next) {
			wps_er_ap_put_message(ap, next);
			wpabuf_free(next);
		} else {
			wpa_printf(MSG_DEBUG, "WPS ER: Failed to build "
				   "message");
			wps_deinit(ap->wps);
			ap->wps = NULL;
		}
	} else if (res == WPS_DONE) {
		wpa_printf(MSG_DEBUG, "WPS ER: Protocol run done");
		wps_deinit(ap->wps);
		ap->wps = NULL;
	} else {
		wpa_printf(MSG_DEBUG, "WPS ER: Failed to process message from "
			   "AP (res=%d)", res);
		wps_deinit(ap->wps);
		ap->wps = NULL;
	}
}


static void wps_er_ap_learn_m1(struct wps_er_ap *ap, struct wpabuf *m1)
{
	struct wps_config cfg;

	if (ap->wps) {
		wpa_printf(MSG_DEBUG, "WPS ER: Protocol run already in "
			   "progress with this AP");
		return;
	}

	os_memset(&cfg, 0, sizeof(cfg));
	cfg.wps = ap->er->wps;
	cfg.registrar = 1;
	ap->wps = wps_init(&cfg);
	if (ap->wps == NULL)
		return;
	ap->wps->ap_settings_cb = wps_er_ap_settings_cb;
	ap->wps->ap_settings_cb_ctx = ap;

	wps_er_ap_process(ap, m1);
}


static void wps_er_ap_learn(struct wps_er_ap *ap, const char *dev_info)
{
	struct wpabuf *info;
	enum http_reply_code ret;

	wpa_printf(MSG_DEBUG, "WPS ER: Received GetDeviceInfo response (M1) "
		   "from the AP");
	info = xml_get_base64_item(dev_info, "NewDeviceInfo", &ret);
	if (info == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: Could not extract "
			   "NewDeviceInfo from GetDeviceInfo response");
		return;
	}

	ap->m1_handler(ap, info);
	wpabuf_free(info);
}


static void wps_er_http_get_dev_info_cb(void *ctx, struct http_client *c,
					enum http_client_event event)
{
	struct wps_er_ap *ap = ctx;
	struct wpabuf *reply;
	char *dev_info = NULL;

	switch (event) {
	case HTTP_CLIENT_OK:
		wpa_printf(MSG_DEBUG, "WPS ER: GetDeviceInfo OK");
		reply = http_client_get_body(c);
		if (reply == NULL)
			break;
		dev_info = os_zalloc(wpabuf_len(reply) + 1);
		if (dev_info == NULL)
			break;
		os_memcpy(dev_info, wpabuf_head(reply), wpabuf_len(reply));
		break;
	case HTTP_CLIENT_FAILED:
	case HTTP_CLIENT_INVALID_REPLY:
	case HTTP_CLIENT_TIMEOUT:
		wpa_printf(MSG_DEBUG, "WPS ER: GetDeviceInfo failed");
		break;
	}
	http_client_free(ap->http);
	ap->http = NULL;

	if (dev_info) {
		wps_er_ap_learn(ap, dev_info);
		os_free(dev_info);
	}
}


static int wps_er_send_get_device_info(struct wps_er_ap *ap,
				       void (*m1_handler)(struct wps_er_ap *ap,
							  struct wpabuf *m1))
{
	struct wpabuf *buf;
	char *len_ptr, *body_ptr;
	struct sockaddr_in dst;
	char *url, *path;

	if (ap->http) {
		wpa_printf(MSG_DEBUG, "WPS ER: Pending HTTP operation ongoing "
			   "with the AP - cannot get device info");
		return -1;
	}

	if (ap->control_url == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: No controlURL for AP");
		return -1;
	}

	url = http_client_url_parse(ap->control_url, &dst, &path);
	if (url == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: Failed to parse controlURL");
		return -1;
	}

	buf = wps_er_soap_hdr(NULL, "GetDeviceInfo", NULL, path, &dst,
			      &len_ptr, &body_ptr);
	os_free(url);
	if (buf == NULL)
		return -1;

	wps_er_soap_end(buf, "GetDeviceInfo", len_ptr, body_ptr);

	ap->http = http_client_addr(&dst, buf, 10000,
				    wps_er_http_get_dev_info_cb, ap);
	if (ap->http == NULL) {
		wpabuf_free(buf);
		return -1;
	}

	ap->m1_handler = m1_handler;

	return 0;
}


int wps_er_learn(struct wps_er *er, const u8 *uuid, const u8 *pin,
		 size_t pin_len)
{
	struct wps_er_ap *ap;

	if (er == NULL)
		return -1;

	ap = wps_er_ap_get(er, NULL, uuid);
	if (ap == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: AP not found for learn "
			   "request");
		return -1;
	}
	if (ap->wps) {
		wpa_printf(MSG_DEBUG, "WPS ER: Pending operation ongoing "
			   "with the AP - cannot start learn");
		return -1;
	}

	if (wps_er_send_get_device_info(ap, wps_er_ap_learn_m1) < 0)
		return -1;

	er->skip_set_sel_reg = 1;
	wps_registrar_add_pin(er->wps->registrar, NULL, uuid, pin, pin_len, 0);
	er->skip_set_sel_reg = 0;

	return 0;
}


int wps_er_set_config(struct wps_er *er, const u8 *uuid,
		      const struct wps_credential *cred)
{
	struct wps_er_ap *ap;

	if (er == NULL)
		return -1;

	ap = wps_er_ap_get(er, NULL, uuid);
	if (ap == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: AP not found for set config "
			   "request");
		return -1;
	}

	os_free(ap->ap_settings);
	ap->ap_settings = os_malloc(sizeof(*cred));
	if (ap->ap_settings == NULL)
		return -1;
	os_memcpy(ap->ap_settings, cred, sizeof(*cred));
	ap->ap_settings->cred_attr = NULL;
	wpa_printf(MSG_DEBUG, "WPS ER: Updated local AP settings based set "
		   "config request");

	return 0;
}


static void wps_er_ap_config_m1(struct wps_er_ap *ap, struct wpabuf *m1)
{
	struct wps_config cfg;

	if (ap->wps) {
		wpa_printf(MSG_DEBUG, "WPS ER: Protocol run already in "
			   "progress with this AP");
		return;
	}

	os_memset(&cfg, 0, sizeof(cfg));
	cfg.wps = ap->er->wps;
	cfg.registrar = 1;
	cfg.new_ap_settings = ap->ap_settings;
	ap->wps = wps_init(&cfg);
	if (ap->wps == NULL)
		return;
	ap->wps->ap_settings_cb = NULL;
	ap->wps->ap_settings_cb_ctx = NULL;

	wps_er_ap_process(ap, m1);
}


int wps_er_config(struct wps_er *er, const u8 *uuid, const u8 *pin,
		  size_t pin_len, const struct wps_credential *cred)
{
	struct wps_er_ap *ap;

	if (er == NULL)
		return -1;

	ap = wps_er_ap_get(er, NULL, uuid);
	if (ap == NULL) {
		wpa_printf(MSG_DEBUG, "WPS ER: AP not found for config "
			   "request");
		return -1;
	}
	if (ap->wps) {
		wpa_printf(MSG_DEBUG, "WPS ER: Pending operation ongoing "
			   "with the AP - cannot start config");
		return -1;
	}

	os_free(ap->ap_settings);
	ap->ap_settings = os_malloc(sizeof(*cred));
	if (ap->ap_settings == NULL)
		return -1;
	os_memcpy(ap->ap_settings, cred, sizeof(*cred));
	ap->ap_settings->cred_attr = NULL;

	if (wps_er_send_get_device_info(ap, wps_er_ap_config_m1) < 0)
		return -1;

	er->skip_set_sel_reg = 1;
	wps_registrar_add_pin(er->wps->registrar, NULL, uuid, pin, pin_len, 0);
	er->skip_set_sel_reg = 0;

	return 0;
}
