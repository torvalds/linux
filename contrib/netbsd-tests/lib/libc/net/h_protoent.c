/* $NetBSD: h_protoent.c,v 1.2 2011/04/07 18:14:09 jruoho Exp $ */

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
pserv(const struct protoent *prp)
{
	char **pp;

	printf("name=%s, proto=%d, aliases=",
	    prp->p_name, prp->p_proto);
	for (pp = prp->p_aliases; *pp; pp++)
		printf("%s ", *pp);
	printf("\n");
}

static void
usage(void)
{
	(void)fprintf(stderr, "Usage: %s\n"
	    "\t%s -p <proto>\n"
	    "\t%s -n <name>\n", getprogname(), getprogname(),
	    getprogname());
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct protoent *prp;
	const char *proto = NULL, *name = NULL;
	int c;

	while ((c = getopt(argc, argv, "p:n:")) != -1) {
		switch (c) {
		case 'n':
			name = optarg;
			break;
		case 'p':
			proto = optarg;
			break;
		default:
			usage();
		}
	}

	if (proto && name)
		usage();
	if (proto) {
		if ((prp = getprotobynumber(atoi(proto))) != NULL)
			pserv(prp);
		return 0;
	}
	if (name) {
		if ((prp = getprotobyname(name)) != NULL)
			pserv(prp);
		return 0;
	}

	setprotoent(0);
	while ((prp = getprotoent()) != NULL)
		pserv(prp);
	endprotoent();
	return 0;
}
