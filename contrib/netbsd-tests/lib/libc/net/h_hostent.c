/*	$NetBSD: h_hostent.c,v 1.2 2014/01/09 02:18:10 christos Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
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
#include <sys/cdefs.h>
__RCSID("$NetBSD: h_hostent.c,v 1.2 2014/01/09 02:18:10 christos Exp $");

#include <stdio.h>
#include <string.h>
#include <nsswitch.h>
#include <netdb.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>

#include <netinet/in.h>
#include <sys/types.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>

#include "hostent.h"

extern const char *__res_conf_name;

static void
phostent(const struct hostent *h)
{
	size_t i;
	char buf[1024];
	const int af = h->h_length == NS_INADDRSZ ? AF_INET : AF_INET6;

	printf("name=%s, length=%d, addrtype=%d, aliases=[",
	    h->h_name, h->h_length, h->h_addrtype);

	for (i = 0; h->h_aliases[i]; i++)
		printf("%s%s", i == 0 ? "" : " ", h->h_aliases[i]);

	printf("] addr_list=[");

	for (i = 0; h->h_addr_list[i]; i++)
		printf("%s%s", i == 0 ? "" : " ", inet_ntop(af,
		    h->h_addr_list[i], buf, (socklen_t)sizeof(buf)));

	printf("]\n");
}

static void
usage(void)
{
	(void)fprintf(stderr, "Usage: %s [-f <hostsfile>] "
	    "[-t <any|dns|nis|files>] "
	    "[-46a] <name|address>\n", getprogname());
	exit(EXIT_FAILURE);
}

static void
getby(int (*f)(void *, void *, va_list), struct getnamaddr *info, ...)
{
	va_list ap;
	int e;

	va_start(ap, info);
	e = (*f)(info, NULL, ap);
	va_end(ap);
	switch (e) {
	case NS_SUCCESS:
		phostent(info->hp);
		break;
	default:
		printf("error %d\n", e);
		break;
	}
}

static void
geta(struct hostent *hp) {
	if (hp == NULL)
		printf("error %d\n", h_errno);
	else
		phostent(hp);
}

int
main(int argc, char *argv[])
{
	int (*f)(void *, void *, va_list) = NULL;
	const char *type = "any";
	int c, af, e, byaddr, len;
	struct hostent hent;
	struct getnamaddr info;
	char buf[4096];
	
	af = AF_INET;
	byaddr = 0;
	len = 0;
	info.hp = &hent;
	info.buf = buf;
	info.buflen = sizeof(buf);
	info.he = &e;

	while ((c = getopt(argc, argv, "46af:r:t:")) != -1) {
		switch (c) {
		case '4':
			af = AF_INET;
			break;
		case '6':
			af = AF_INET6;
			break;
		case 'a':
			byaddr++;
			break;
		case 'f':
			_hf_sethostsfile(optarg);
			break;
		case 'r':
			__res_conf_name = optarg;
			break;
		case 't':
			type = optarg;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	switch (*type) {
	case 'a':
		break;
	case 'd':
		f = byaddr ? _dns_gethtbyaddr : _dns_gethtbyname;
		break;
#ifdef YP
	case 'n':
		f = byaddr ? _yp_gethtbyaddr : _yp_gethtbyname;
		break;
#endif
	case 'f':
		f = byaddr ? _hf_gethtbyaddr : _hf_gethtbyname;
		break;
	default:
		errx(EXIT_FAILURE, "Unknown db type `%s'", type);
	}

	if (byaddr) {
		struct in6_addr addr;
		af = strchr(*argv, ':') ? AF_INET6 : AF_INET;
		len = af == AF_INET ? NS_INADDRSZ : NS_IN6ADDRSZ;
		if (inet_pton(af, *argv, &addr) == -1)
			err(EXIT_FAILURE, "Can't parse `%s'", *argv);
		if (*type == 'a')
			geta(gethostbyaddr((const char *)&addr, len, af));
		else
			getby(f, &info, &addr, len, af);
	} else {
		if (*type == 'a')
			geta(gethostbyname2(*argv, af));
		else
			getby(f, &info, *argv, len, af);
	}
		
	return 0;
}
