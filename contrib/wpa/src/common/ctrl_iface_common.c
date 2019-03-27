/*
 * Common hostapd/wpa_supplicant ctrl iface code.
 * Copyright (c) 2002-2013, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2015, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"
#include <netdb.h>
#include <sys/un.h>

#include "utils/common.h"
#include "ctrl_iface_common.h"

static int sockaddr_compare(struct sockaddr_storage *a, socklen_t a_len,
			    struct sockaddr_storage *b, socklen_t b_len)
{
	if (a->ss_family != b->ss_family)
		return 1;

	switch (a->ss_family) {
#ifdef CONFIG_CTRL_IFACE_UDP
	case AF_INET:
	{
		struct sockaddr_in *in_a, *in_b;

		in_a = (struct sockaddr_in *) a;
		in_b = (struct sockaddr_in *) b;

		if (in_a->sin_port != in_b->sin_port)
			return 1;
		if (in_a->sin_addr.s_addr != in_b->sin_addr.s_addr)
			return 1;
		break;
	}
	case AF_INET6:
	{
		struct sockaddr_in6 *in6_a, *in6_b;

		in6_a = (struct sockaddr_in6 *) a;
		in6_b = (struct sockaddr_in6 *) b;

		if (in6_a->sin6_port != in6_b->sin6_port)
			return 1;
		if (os_memcmp(&in6_a->sin6_addr, &in6_b->sin6_addr,
			      sizeof(in6_a->sin6_addr)) != 0)
			return 1;
		break;
	}
#endif /* CONFIG_CTRL_IFACE_UDP */
#ifdef CONFIG_CTRL_IFACE_UNIX
	case AF_UNIX:
	{
		struct sockaddr_un *u_a, *u_b;

		u_a = (struct sockaddr_un *) a;
		u_b = (struct sockaddr_un *) b;

		if (a_len != b_len ||
		    os_memcmp(u_a->sun_path, u_b->sun_path,
			      a_len - offsetof(struct sockaddr_un, sun_path))
		    != 0)
			return 1;
		break;
	}
#endif /* CONFIG_CTRL_IFACE_UNIX */
	default:
		return 1;
	}

	return 0;
}


void sockaddr_print(int level, const char *msg, struct sockaddr_storage *sock,
		    socklen_t socklen)
{
	switch (sock->ss_family) {
#ifdef CONFIG_CTRL_IFACE_UDP
	case AF_INET:
	case AF_INET6:
	{
		char host[NI_MAXHOST] = { 0 };
		char service[NI_MAXSERV] = { 0 };

		getnameinfo((struct sockaddr *) sock, socklen,
			    host, sizeof(host),
			    service, sizeof(service),
			    NI_NUMERICHOST);

		wpa_printf(level, "%s %s:%s", msg, host, service);
		break;
	}
#endif /* CONFIG_CTRL_IFACE_UDP */
#ifdef CONFIG_CTRL_IFACE_UNIX
	case AF_UNIX:
	{
		char addr_txt[200];

		printf_encode(addr_txt, sizeof(addr_txt),
			      (u8 *) ((struct sockaddr_un *) sock)->sun_path,
			      socklen - offsetof(struct sockaddr_un, sun_path));
		wpa_printf(level, "%s %s", msg, addr_txt);
		break;
	}
#endif /* CONFIG_CTRL_IFACE_UNIX */
	default:
		wpa_printf(level, "%s", msg);
		break;
	}
}


static int ctrl_set_events(struct wpa_ctrl_dst *dst, const char *input)
{
	const char *value;
	int val;

	if (!input)
		return 0;

	value = os_strchr(input, '=');
	if (!value)
		return -1;
	value++;
	val = atoi(value);
	if (val < 0 || val > 1)
		return -1;

	if (str_starts(input, "probe_rx_events=")) {
		if (val)
			dst->events |= WPA_EVENT_RX_PROBE_REQUEST;
		else
			dst->events &= ~WPA_EVENT_RX_PROBE_REQUEST;
	}

	return 0;
}


int ctrl_iface_attach(struct dl_list *ctrl_dst, struct sockaddr_storage *from,
		      socklen_t fromlen, const char *input)
{
	struct wpa_ctrl_dst *dst;

	/* Update event registration if already attached */
	dl_list_for_each(dst, ctrl_dst, struct wpa_ctrl_dst, list) {
		if (!sockaddr_compare(from, fromlen,
				      &dst->addr, dst->addrlen))
			return ctrl_set_events(dst, input);
	}

	/* New attachment */
	dst = os_zalloc(sizeof(*dst));
	if (dst == NULL)
		return -1;
	os_memcpy(&dst->addr, from, fromlen);
	dst->addrlen = fromlen;
	dst->debug_level = MSG_INFO;
	ctrl_set_events(dst, input);
	dl_list_add(ctrl_dst, &dst->list);

	sockaddr_print(MSG_DEBUG, "CTRL_IFACE monitor attached", from, fromlen);
	return 0;
}


int ctrl_iface_detach(struct dl_list *ctrl_dst, struct sockaddr_storage *from,
		      socklen_t fromlen)
{
	struct wpa_ctrl_dst *dst;

	dl_list_for_each(dst, ctrl_dst, struct wpa_ctrl_dst, list) {
		if (!sockaddr_compare(from, fromlen,
				      &dst->addr, dst->addrlen)) {
			sockaddr_print(MSG_DEBUG, "CTRL_IFACE monitor detached",
				       from, fromlen);
			dl_list_del(&dst->list);
			os_free(dst);
			return 0;
		}
	}

	return -1;
}


int ctrl_iface_level(struct dl_list *ctrl_dst, struct sockaddr_storage *from,
		     socklen_t fromlen, const char *level)
{
	struct wpa_ctrl_dst *dst;

	wpa_printf(MSG_DEBUG, "CTRL_IFACE LEVEL %s", level);

	dl_list_for_each(dst, ctrl_dst, struct wpa_ctrl_dst, list) {
		if (!sockaddr_compare(from, fromlen,
				      &dst->addr, dst->addrlen)) {
			sockaddr_print(MSG_DEBUG,
				       "CTRL_IFACE changed monitor level",
				       from, fromlen);
			dst->debug_level = atoi(level);
			return 0;
		}
	}

	return -1;
}
