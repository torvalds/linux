/*
 * IP address processing
 * Copyright (c) 2003-2006, Jouni Malinen <j@w1.fi>
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
#include "ip_addr.h"

const char * hostapd_ip_txt(const struct hostapd_ip_addr *addr, char *buf,
			    size_t buflen)
{
	if (buflen == 0 || addr == NULL)
		return NULL;

	if (addr->af == AF_INET) {
		os_strlcpy(buf, inet_ntoa(addr->u.v4), buflen);
	} else {
		buf[0] = '\0';
	}
#ifdef CONFIG_IPV6
	if (addr->af == AF_INET6) {
		if (inet_ntop(AF_INET6, &addr->u.v6, buf, buflen) == NULL)
			buf[0] = '\0';
	}
#endif /* CONFIG_IPV6 */

	return buf;
}


int hostapd_ip_diff(struct hostapd_ip_addr *a, struct hostapd_ip_addr *b)
{
	if (a == NULL && b == NULL)
		return 0;
	if (a == NULL || b == NULL)
		return 1;

	switch (a->af) {
	case AF_INET:
		if (a->u.v4.s_addr != b->u.v4.s_addr)
			return 1;
		break;
#ifdef CONFIG_IPV6
	case AF_INET6:
		if (os_memcmp(&a->u.v6, &b->u.v6, sizeof(a->u.v6)) != 0)
			return 1;
		break;
#endif /* CONFIG_IPV6 */
	}

	return 0;
}


int hostapd_parse_ip_addr(const char *txt, struct hostapd_ip_addr *addr)
{
#ifndef CONFIG_NATIVE_WINDOWS
	if (inet_aton(txt, &addr->u.v4)) {
		addr->af = AF_INET;
		return 0;
	}

#ifdef CONFIG_IPV6
	if (inet_pton(AF_INET6, txt, &addr->u.v6) > 0) {
		addr->af = AF_INET6;
		return 0;
	}
#endif /* CONFIG_IPV6 */
#endif /* CONFIG_NATIVE_WINDOWS */

	return -1;
}
