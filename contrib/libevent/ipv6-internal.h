/*
 * Copyright (c) 2009-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Internal use only: Fake IPv6 structures and values on platforms that
 * do not have them */

#ifndef IPV6_INTERNAL_H_INCLUDED_
#define IPV6_INTERNAL_H_INCLUDED_

#include "event2/event-config.h"
#include "evconfig-private.h"

#include <sys/types.h>
#ifdef EVENT__HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#include "event2/util.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @file ipv6-internal.h
 *
 * Replacement types and functions for platforms that don't support ipv6
 * properly.
 */

#ifndef EVENT__HAVE_STRUCT_IN6_ADDR
struct in6_addr {
	ev_uint8_t s6_addr[16];
};
#endif

#ifndef EVENT__HAVE_SA_FAMILY_T
typedef int sa_family_t;
#endif

#ifndef EVENT__HAVE_STRUCT_SOCKADDR_IN6
struct sockaddr_in6 {
	/* This will fail if we find a struct sockaddr that doesn't have
	 * sa_family as the first element. */
	sa_family_t sin6_family;
	ev_uint16_t sin6_port;
	struct in6_addr sin6_addr;
};
#endif

#ifndef AF_INET6
#define AF_INET6 3333
#endif
#ifndef PF_INET6
#define PF_INET6 AF_INET6
#endif

#ifdef __cplusplus
}
#endif

#endif
