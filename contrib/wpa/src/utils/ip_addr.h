/*
 * IP address processing
 * Copyright (c) 2003-2006, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef IP_ADDR_H
#define IP_ADDR_H

struct hostapd_ip_addr {
	int af; /* AF_INET / AF_INET6 */
	union {
		struct in_addr v4;
#ifdef CONFIG_IPV6
		struct in6_addr v6;
#endif /* CONFIG_IPV6 */
		u8 max_len[16];
	} u;
};

const char * hostapd_ip_txt(const struct hostapd_ip_addr *addr, char *buf,
			    size_t buflen);
int hostapd_parse_ip_addr(const char *txt, struct hostapd_ip_addr *addr);

#endif /* IP_ADDR_H */
