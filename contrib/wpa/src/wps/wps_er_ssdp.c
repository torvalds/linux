/*
 * Wi-Fi Protected Setup - External Registrar (SSDP)
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "uuid.h"
#include "eloop.h"
#include "wps_i.h"
#include "wps_upnp.h"
#include "wps_upnp_i.h"
#include "wps_er.h"


static void wps_er_ssdp_rx(int sd, void *eloop_ctx, void *sock_ctx)
{
	struct wps_er *er = eloop_ctx;
	struct sockaddr_in addr; /* client address */
	socklen_t addr_len;
	int nread;
	char buf[MULTICAST_MAX_READ], *pos, *pos2, *start;
	int wfa = 0, byebye = 0;
	int max_age = -1;
	char *location = NULL;
	u8 uuid[WPS_UUID_LEN];

	addr_len = sizeof(addr);
	nread = recvfrom(sd, buf, sizeof(buf) - 1, 0,
			 (struct sockaddr *) &addr, &addr_len);
	if (nread <= 0)
		return;
	buf[nread] = '\0';
	if (er->filter_addr.s_addr &&
	    er->filter_addr.s_addr != addr.sin_addr.s_addr)
		return;

	wpa_printf(MSG_DEBUG, "WPS ER: Received SSDP from %s",
		   inet_ntoa(addr.sin_addr));
	wpa_hexdump_ascii(MSG_MSGDUMP, "WPS ER: Received SSDP contents",
			  (u8 *) buf, nread);

	if (sd == er->multicast_sd) {
		/* Reply to M-SEARCH */
		if (os_strncmp(buf, "HTTP/1.1 200 OK", 15) != 0)
			return; /* unexpected response header */
	} else {
		/* Unsolicited message (likely NOTIFY or M-SEARCH) */
		if (os_strncmp(buf, "NOTIFY ", 7) != 0)
			return; /* only process notifications */
	}

	os_memset(uuid, 0, sizeof(uuid));

	for (start = buf; start && *start; start = pos) {
		pos = os_strchr(start, '\n');
		if (pos) {
			if (pos[-1] == '\r')
				pos[-1] = '\0';
			*pos++ = '\0';
		}
		if (os_strstr(start, "schemas-wifialliance-org:device:"
			      "WFADevice:1"))
			wfa = 1;
		if (os_strstr(start, "schemas-wifialliance-org:service:"
			      "WFAWLANConfig:1"))
			wfa = 1;
		if (os_strncasecmp(start, "LOCATION:", 9) == 0) {
			start += 9;
			while (*start == ' ')
				start++;
			location = start;
		} else if (os_strncasecmp(start, "NTS:", 4) == 0) {
			if (os_strstr(start, "ssdp:byebye"))
				byebye = 1;
		} else if (os_strncasecmp(start, "CACHE-CONTROL:", 14) == 0) {
			start += 14;
			pos2 = os_strstr(start, "max-age=");
			if (pos2 == NULL)
				continue;
			pos2 += 8;
			max_age = atoi(pos2);
		} else if (os_strncasecmp(start, "USN:", 4) == 0) {
			start += 4;
			pos2 = os_strstr(start, "uuid:");
			if (pos2) {
				pos2 += 5;
				while (*pos2 == ' ')
					pos2++;
				if (uuid_str2bin(pos2, uuid) < 0) {
					wpa_printf(MSG_DEBUG, "WPS ER: "
						   "Invalid UUID in USN: %s",
						   pos2);
					return;
				}
			}
		}
	}

	if (!wfa)
		return; /* Not WPS advertisement/reply */

	if (byebye) {
		wps_er_ap_cache_settings(er, &addr.sin_addr);
		wps_er_ap_remove(er, &addr.sin_addr);
		return;
	}

	if (!location)
		return; /* Unknown location */

	if (max_age < 1)
		return; /* No max-age reported */

	wpa_printf(MSG_DEBUG, "WPS ER: AP discovered: %s "
		   "(packet source: %s  max-age: %d)",
		   location, inet_ntoa(addr.sin_addr), max_age);

	wps_er_ap_add(er, uuid, &addr.sin_addr, location, max_age);
}


void wps_er_send_ssdp_msearch(struct wps_er *er)
{
	struct wpabuf *msg;
	struct sockaddr_in dest;

	msg = wpabuf_alloc(500);
	if (msg == NULL)
		return;

	wpabuf_put_str(msg,
		       "M-SEARCH * HTTP/1.1\r\n"
		       "HOST: 239.255.255.250:1900\r\n"
		       "MAN: \"ssdp:discover\"\r\n"
		       "MX: 3\r\n"
		       "ST: urn:schemas-wifialliance-org:device:WFADevice:1"
		       "\r\n"
		       "\r\n");

	os_memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = inet_addr(UPNP_MULTICAST_ADDRESS);
	dest.sin_port = htons(UPNP_MULTICAST_PORT);

	if (sendto(er->multicast_sd, wpabuf_head(msg), wpabuf_len(msg), 0,
		   (struct sockaddr *) &dest, sizeof(dest)) < 0)
		wpa_printf(MSG_DEBUG, "WPS ER: M-SEARCH sendto failed: "
			   "%d (%s)", errno, strerror(errno));

	wpabuf_free(msg);
}


int wps_er_ssdp_init(struct wps_er *er)
{
	if (add_ssdp_network(er->ifname)) {
		wpa_printf(MSG_INFO, "WPS ER: Failed to add routing entry for "
			   "SSDP");
		return -1;
	}

	er->multicast_sd = ssdp_open_multicast_sock(er->ip_addr,
						    er->forced_ifname ?
						    er->ifname : NULL);
	if (er->multicast_sd < 0) {
		wpa_printf(MSG_INFO, "WPS ER: Failed to open multicast socket "
			   "for SSDP");
		return -1;
	}

	er->ssdp_sd = ssdp_listener_open();
	if (er->ssdp_sd < 0) {
		wpa_printf(MSG_INFO, "WPS ER: Failed to open SSDP listener "
			   "socket");
		return -1;
	}

	if (eloop_register_sock(er->multicast_sd, EVENT_TYPE_READ,
				wps_er_ssdp_rx, er, NULL) ||
	    eloop_register_sock(er->ssdp_sd, EVENT_TYPE_READ,
				wps_er_ssdp_rx, er, NULL))
		return -1;

	wps_er_send_ssdp_msearch(er);

	return 0;
}


void wps_er_ssdp_deinit(struct wps_er *er)
{
	if (er->multicast_sd >= 0) {
		eloop_unregister_sock(er->multicast_sd, EVENT_TYPE_READ);
		close(er->multicast_sd);
	}
	if (er->ssdp_sd >= 0) {
		eloop_unregister_sock(er->ssdp_sd, EVENT_TYPE_READ);
		close(er->ssdp_sd);
	}
}
