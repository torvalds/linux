/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: alist_new.c,v 1.5.2.2 2012/07/22 08:04:24 darren_r Exp $
 */

#include "ipf.h"
#include <ctype.h>

alist_t *
alist_new(int family, char *host)
{
	int a, b, c, d, bits;
	char *slash;
	alist_t *al;
	u_int mask;

	if (family == AF_UNSPEC) {
		if (strchr(host, ':') != NULL)
			family = AF_INET6;
		else
			family = AF_INET;
	}
	if (family != AF_INET && family != AF_INET6)
		return NULL;

	al = calloc(1, sizeof(*al));
	if (al == NULL) {
		fprintf(stderr, "alist_new out of memory\n");
		return NULL;
	}

	while (ISSPACE(*host))
		host++;

	if (*host == '!') {
		al->al_not = 1;
		host++;
		while (ISSPACE(*host))
			host++;
	}

	bits = -1;
	slash = strchr(host, '/');
	if (slash != NULL) {
		*slash = '\0';
		bits = atoi(slash + 1);
	}

	if (family == AF_INET) {
		if (bits > 32)
			goto bad;

		a = b = c = d = -1;
		sscanf(host, "%d.%d.%d.%d", &a, &b, &c, &d);

		if (bits > 0 && bits < 33) {
			mask = 0xffffffff << (32 - bits);
		} else if (b == -1) {
			mask = 0xff000000;
			b = c = d = 0;
		} else if (c == -1) {
			mask = 0xffff0000;
			c = d = 0;
		} else if (d == -1) {
			mask = 0xffffff00;
			d = 0;
		} else {
			mask = 0xffffffff;
		}
		al->al_mask = htonl(mask);
	} else {
		if (bits > 128)
			goto bad;
		fill6bits(bits, al->al_i6mask.i6);
	}

	if (gethost(family, host, &al->al_i6addr) == -1) {
		if (slash != NULL)
			*slash = '/';
		fprintf(stderr, "Cannot parse hostname\n");
		goto bad;
	}
	al->al_family = family;
	if (slash != NULL)
		*slash = '/';
	return al;
bad:
	free(al);
	return NULL;
}
