/* $NetBSD: h_servent.c,v 1.2 2011/04/07 18:14:09 jruoho Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
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

#include <netdb.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <stdio.h>

static void
pserv(const struct servent *svp)
{
	char **pp;

	printf("name=%s, port=%d, proto=%s, aliases=",
	    svp->s_name, ntohs((uint16_t)svp->s_port), svp->s_proto);
	for (pp = svp->s_aliases; *pp; pp++)
		printf("%s ", *pp);
	printf("\n");
}

static void
usage(void)
{
	(void)fprintf(stderr, "Usage: %s\n"
	    "\t%s -p <port> [-P <proto>]\n"
	    "\t%s -n <name> [-P <proto>]\n", getprogname(), getprogname(),
	    getprogname());
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct servent *svp;
	const char *port = NULL, *proto = NULL, *name = NULL;
	int c;

	while ((c = getopt(argc, argv, "p:n:P:")) != -1) {
		switch (c) {
		case 'n':
			name = optarg;
			break;
		case 'p':
			port = optarg;
			break;
		case 'P':
			proto = optarg;
			break;
		default:
			usage();
		}
	}

	if (port && name)
		usage();
	if (port) {
		if ((svp = getservbyport(htons(atoi(port)), proto)) != NULL)
			pserv(svp);
		return 0;
	}
	if (name) {
		if ((svp = getservbyname(name, proto)) != NULL)
			pserv(svp);
		return 0;
	}

	setservent(0);
	while ((svp = getservent()) != NULL)
		pserv(svp);
	endservent();
	return 0;
}
