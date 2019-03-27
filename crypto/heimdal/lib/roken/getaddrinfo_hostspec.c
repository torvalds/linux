/*
 * Copyright (c) 2000 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>

#include "roken.h"

/* getaddrinfo via string specifying host and port */

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
roken_getaddrinfo_hostspec2(const char *hostspec,
			    int socktype,
			    int port,
			    struct addrinfo **ai)
{
    const char *p;
    char portstr[NI_MAXSERV];
    char host[MAXHOSTNAMELEN];
    struct addrinfo hints;
    int hostspec_len;

    struct hst {
	const char *prefix;
	int socktype;
	int protocol;
	int port;
    } *hstp, hst[] = {
	{ "http://", SOCK_STREAM, IPPROTO_TCP, 80 },
	{ "http/", SOCK_STREAM, IPPROTO_TCP, 80 },
	{ "tcp/", SOCK_STREAM, IPPROTO_TCP, 0 },
	{ "udp/", SOCK_DGRAM, IPPROTO_UDP, 0 },
	{ NULL, 0, 0, 0 }
    };

    memset(&hints, 0, sizeof(hints));

    hints.ai_socktype = socktype;

    for(hstp = hst; hstp->prefix; hstp++) {
	if(strncmp(hostspec, hstp->prefix, strlen(hstp->prefix)) == 0) {
	    hints.ai_socktype = hstp->socktype;
	    hints.ai_protocol = hstp->protocol;
	    if(port == 0)
		port = hstp->port;
	    hostspec += strlen(hstp->prefix);
	    break;
	}
    }

    p = strchr (hostspec, ':');
    if (p != NULL) {
	char *end;

	port = strtol (p + 1, &end, 0);
	hostspec_len = p - hostspec;
    } else {
	hostspec_len = strlen(hostspec);
    }
    snprintf (portstr, sizeof(portstr), "%u", port);

    snprintf (host, sizeof(host), "%.*s", hostspec_len, hostspec);
    return getaddrinfo (host, portstr, &hints, ai);
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
roken_getaddrinfo_hostspec(const char *hostspec,
			   int port,
			   struct addrinfo **ai)
{
    return roken_getaddrinfo_hostspec2(hostspec, 0, port, ai);
}
