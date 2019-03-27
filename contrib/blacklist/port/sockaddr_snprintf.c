/*	$NetBSD: sockaddr_snprintf.c,v 1.11 2016/06/01 22:57:51 christos Exp $	*/

/*-
 * Copyright (c) 2004 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: sockaddr_snprintf.c,v 1.11 2016/06/01 22:57:51 christos Exp $");
#endif /* LIBC_SCCS and not lint */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <netinet/in.h>
#ifdef __linux__
#undef HAVE_NETATALK_AT_H
#endif
#ifdef HAVE_NETATALK_AT_H
#include <netatalk/at.h>
#endif
#ifdef HAVE_NET_IF_DL_H
#include <net/if_dl.h>
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#ifdef HAVE_UTIL_H
#include <util.h>
#endif
#include <netdb.h>

#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
#define SLEN(a)	(a)->a ## _len
#else
static socklen_t
socklen(u_int af)
{
	switch (af) {
	case AF_INET:
		return sizeof(struct sockaddr_in);
	case AF_INET6:
		return sizeof(struct sockaddr_in6);
	case AF_LOCAL:
		return sizeof(struct sockaddr_un);
#ifdef HAVE_NET_IF_DL_H
	case AF_LINK:
		return sizeof(struct sockaddr_dl);
#endif
#ifdef HAVE_NETATALK_AT_H
	case AF_APPLETALK:
		return sizeof(struct sockaddr_at);
#endif
	default:
		return sizeof(struct sockaddr_storage);
	}
}

#define SLEN(a)	socklen((a)->a ## _family)
#endif

#ifdef HAVE_NETATALK_AT_H
static int
debug_at(char *str, size_t len, const struct sockaddr_at *sat)
{
	return snprintf(str, len, "sat_len=%u, sat_family=%u, sat_port=%u, "
	    "sat_addr.s_net=%u, sat_addr.s_node=%u, "
	    "sat_range.r_netrange.nr_phase=%u, "
	    "sat_range.r_netrange.nr_firstnet=%u, "
	    "sat_range.r_netrange.nr_lastnet=%u",
	    SLEN(sat), sat->sat_family, sat->sat_port,
	    sat->sat_addr.s_net, sat->sat_addr.s_node,
	    sat->sat_range.r_netrange.nr_phase,
	    sat->sat_range.r_netrange.nr_firstnet,
	    sat->sat_range.r_netrange.nr_lastnet);
}
#endif

static int
debug_in(char *str, size_t len, const struct sockaddr_in *sin)
{
	return snprintf(str, len, "sin_len=%u, sin_family=%u, sin_port=%u, "
	    "sin_addr.s_addr=%08x",
	    SLEN(sin), sin->sin_family, sin->sin_port,
	    sin->sin_addr.s_addr);
}

static int
debug_in6(char *str, size_t len, const struct sockaddr_in6 *sin6)
{
	const uint8_t *s = sin6->sin6_addr.s6_addr;

	return snprintf(str, len, "sin6_len=%u, sin6_family=%u, sin6_port=%u, "
	    "sin6_flowinfo=%u, "
	    "sin6_addr=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:"
	    "%02x:%02x:%02x:%02x:%02x:%02x, sin6_scope_id=%u",
	    SLEN(sin6), sin6->sin6_family, sin6->sin6_port,
	    sin6->sin6_flowinfo, s[0x0], s[0x1], s[0x2], s[0x3], s[0x4], s[0x5],
	    s[0x6], s[0x7], s[0x8], s[0x9], s[0xa], s[0xb], s[0xc], s[0xd],
	    s[0xe], s[0xf], sin6->sin6_scope_id);
}

static int
debug_un(char *str, size_t len, const struct sockaddr_un *sun)
{
	return snprintf(str, len, "sun_len=%u, sun_family=%u, sun_path=%*s",
	    SLEN(sun), sun->sun_family, (int)sizeof(sun->sun_path),
	    sun->sun_path);
}

#ifdef HAVE_NET_IF_DL_H
static int
debug_dl(char *str, size_t len, const struct sockaddr_dl *sdl)
{
	const uint8_t *s = (const void *)sdl->sdl_data;

	return snprintf(str, len, "sdl_len=%u, sdl_family=%u, sdl_index=%u, "
	    "sdl_type=%u, sdl_nlen=%u, sdl_alen=%u, sdl_slen=%u, sdl_data="
	    "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
	    SLEN(sdl), sdl->sdl_family, sdl->sdl_index,
	    sdl->sdl_type, sdl->sdl_nlen, sdl->sdl_alen, sdl->sdl_slen,
	    s[0x0], s[0x1], s[0x2], s[0x3], s[0x4], s[0x5],
	    s[0x6], s[0x7], s[0x8], s[0x9], s[0xa], s[0xb]);
}
#endif

int
sockaddr_snprintf(char * const sbuf, const size_t len, const char * const fmt,
    const struct sockaddr * const sa)
{
	const void *a = NULL;
	char abuf[1024], nbuf[1024], *addr = NULL;

	char Abuf[1024], pbuf[32], *name = NULL, *port = NULL;
	char *ebuf = &sbuf[len - 1], *buf = sbuf;
	const char *ptr, *s;
	int p = -1;
#ifdef HAVE_NETATALK_AT_H
	const struct sockaddr_at *sat = NULL;
#endif
	const struct sockaddr_in *sin4 = NULL;
	const struct sockaddr_in6 *sin6 = NULL;
	const struct sockaddr_un *sun = NULL;
#ifdef HAVE_NET_IF_DL_H
	const struct sockaddr_dl *sdl = NULL;
	char *w = NULL;
#endif
	int na = 1;

#define ADDC(c) do { if (buf < ebuf) *buf++ = c; else buf++; } \
	while (/*CONSTCOND*/0)
#define ADDS(p) do { for (s = p; *s; s++) ADDC(*s); } \
	while (/*CONSTCOND*/0)
#define ADDNA() do { if (na) ADDS("N/A"); } \
	while (/*CONSTCOND*/0)

	switch (sa->sa_family) {
	case AF_UNSPEC:
		goto done;
#ifdef HAVE_NETATALK_AT_H
	case AF_APPLETALK:
		sat = ((const struct sockaddr_at *)(const void *)sa);
		p = ntohs(sat->sat_port);
		(void)snprintf(addr = abuf, sizeof(abuf), "%u.%u",
			ntohs(sat->sat_addr.s_net), sat->sat_addr.s_node);
		(void)snprintf(port = pbuf, sizeof(pbuf), "%d", p);
		break;
#endif
	case AF_LOCAL:
		sun = ((const struct sockaddr_un *)(const void *)sa);
		(void)strlcpy(addr = abuf, sun->sun_path, sizeof(abuf));
		break;
	case AF_INET:
		sin4 = ((const struct sockaddr_in *)(const void *)sa);
		p = ntohs(sin4->sin_port);
		a = &sin4->sin_addr;
		break;
	case AF_INET6:
		sin6 = ((const struct sockaddr_in6 *)(const void *)sa);
		p = ntohs(sin6->sin6_port);
		a = &sin6->sin6_addr;
		break;
#ifdef HAVE_NET_IF_DL_H
	case AF_LINK:
		sdl = ((const struct sockaddr_dl *)(const void *)sa);
		(void)strlcpy(addr = abuf, link_ntoa(sdl), sizeof(abuf));
		if ((w = strchr(addr, ':')) != NULL) {
			*w++ = '\0';
			addr = w;
		}
		break;
#endif
	default:
		errno = EAFNOSUPPORT;
		return -1;
	}

	if (addr == abuf)
		name = addr;

	if (a && getnameinfo(sa, (socklen_t)SLEN(sa), addr = abuf,
	    (unsigned int)sizeof(abuf), NULL, 0,
	    NI_NUMERICHOST|NI_NUMERICSERV) != 0)
		return -1;

	for (ptr = fmt; *ptr; ptr++) {
		if (*ptr != '%') {
			ADDC(*ptr);
			continue;
		}
	  next_char:
		switch (*++ptr) {
		case '?':
			na = 0;
			goto next_char;
		case 'a':
			ADDS(addr);
			break;
		case 'p':
			if (p != -1) {
				(void)snprintf(nbuf, sizeof(nbuf), "%d", p);
				ADDS(nbuf);
			} else
				ADDNA();
			break;
		case 'f':
			(void)snprintf(nbuf, sizeof(nbuf), "%d", sa->sa_family);
			ADDS(nbuf);
			break;
		case 'l':
			(void)snprintf(nbuf, sizeof(nbuf), "%d", SLEN(sa));
			ADDS(nbuf);
			break;
		case 'A':
			if (name)
				ADDS(name);
			else if (!a)
				ADDNA();
			else {
				getnameinfo(sa, (socklen_t)SLEN(sa),
					name = Abuf,
					(unsigned int)sizeof(nbuf), NULL, 0, 0);
				ADDS(name);
			}
			break;
		case 'P':
			if (port)
				ADDS(port);
			else if (p == -1)
				ADDNA();
			else {
				getnameinfo(sa, (socklen_t)SLEN(sa), NULL, 0,
					port = pbuf,
					(unsigned int)sizeof(pbuf), 0);
				ADDS(port);
			}
			break;
		case 'I':
#ifdef HAVE_NET_IF_DL_H
			if (sdl && addr != abuf) {
				ADDS(abuf);
			} else
#endif
			{
				ADDNA();
			}
			break;
		case 'F':
			if (sin6) {
				(void)snprintf(nbuf, sizeof(nbuf), "%d",
				    sin6->sin6_flowinfo);
				ADDS(nbuf);
				break;
			} else {
				ADDNA();
			}
			break;
		case 'S':
			if (sin6) {
				(void)snprintf(nbuf, sizeof(nbuf), "%d",
				    sin6->sin6_scope_id);
				ADDS(nbuf);
				break;
			} else {
				ADDNA();
			}
			break;
		case 'R':
#ifdef HAVE_NETATALK_AT_H
			if (sat) {
				const struct netrange *n =
				    &sat->sat_range.r_netrange;
				(void)snprintf(nbuf, sizeof(nbuf),
				    "%d:[%d,%d]", n->nr_phase , n->nr_firstnet,
				    n->nr_lastnet);
				ADDS(nbuf);
			} else
#endif
			{
				ADDNA();
			}
			break;
		case 'D':
			switch (sa->sa_family) {
#ifdef HAVE_NETATALK_AT_H
			case AF_APPLETALK:
				debug_at(nbuf, sizeof(nbuf), sat);
				break;
#endif
			case AF_LOCAL:
				debug_un(nbuf, sizeof(nbuf), sun);
				break;
			case AF_INET:
				debug_in(nbuf, sizeof(nbuf), sin4);
				break;
			case AF_INET6:
				debug_in6(nbuf, sizeof(nbuf), sin6);
				break;
#ifdef HAVE_NET_IF_DL_H
			case AF_LINK:
				debug_dl(nbuf, sizeof(nbuf), sdl);
				break;
#endif
			default:
				abort();
			}
			ADDS(nbuf);
			break;
		default:
			ADDC('%');
			if (na == 0)
				ADDC('?');
			if (*ptr == '\0')
				goto done;
			/*FALLTHROUGH*/
		case '%':
			ADDC(*ptr);
			break;
		}
		na = 1;
	}
done:
	if (buf < ebuf)
		*buf = '\0';
	else if (len != 0)
		sbuf[len - 1] = '\0';
	return (int)(buf - sbuf);
}
