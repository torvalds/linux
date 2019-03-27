/*
 * Copyright (c) 1999 - 2000 Kungliga Tekniska Högskolan
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
#include "getarg.h"

static int flags;
static int family;
static int socktype;

static int verbose_counter;
static int version_flag;
static int help_flag;

static struct getargs args[] = {
    {"verbose",	0,	arg_counter,	&verbose_counter,"verbose",	NULL},
    {"flags",	0,	arg_integer,	&flags,		"flags",	NULL},
    {"family",	0,	arg_integer,	&family,	"family",	NULL},
    {"socktype",0,	arg_integer,	&socktype,	"socktype",	NULL},
    {"version",	0,	arg_flag,	&version_flag,	"print version",NULL},
    {"help",	0,	arg_flag,	&help_flag,	NULL,		NULL}
};

static void
usage(int ret)
{
    arg_printusage (args,
		    sizeof(args) / sizeof(args[0]),
		    NULL,
		    "[nodename servname...]");
    exit (ret);
}

static void
doit (const char *nodename, const char *servname)
{
    struct addrinfo hints;
    struct addrinfo *res, *r;
    int ret;

    if (verbose_counter)
	printf ("(%s,%s)... ", nodename ? nodename : "null", servname);

    memset (&hints, 0, sizeof(hints));
    hints.ai_flags    = flags;
    hints.ai_family   = family;
    hints.ai_socktype = socktype;

    ret = getaddrinfo (nodename, servname, &hints, &res);
    if (ret)
	errx(1, "error: %s\n", gai_strerror(ret));

    if (verbose_counter)
	printf ("\n");

    for (r = res; r != NULL; r = r->ai_next) {
	char addrstr[256];

	if (inet_ntop (r->ai_family,
		       socket_get_address (r->ai_addr),
		       addrstr, sizeof(addrstr)) == NULL) {
	    if (verbose_counter)
		printf ("\tbad address?\n");
	    continue;
	}
	if (verbose_counter) {
	    printf ("\tfamily = %d, socktype = %d, protocol = %d, "
		    "address = \"%s\", port = %d",
		    r->ai_family, r->ai_socktype, r->ai_protocol,
		    addrstr,
		    ntohs(socket_get_port (r->ai_addr)));
	    if (r->ai_canonname)
		printf (", canonname = \"%s\"", r->ai_canonname);
	    printf ("\n");
	}
    }
    freeaddrinfo (res);
}

int
main(int argc, char **argv)
{
    int optidx = 0;
    int i;

    setprogname (argv[0]);

    if (getarg (args, sizeof(args) / sizeof(args[0]), argc, argv,
		&optidx))
	usage (1);

    if (help_flag)
	usage (0);

    if (version_flag) {
	fprintf (stderr, "%s from %s-%s\n", getprogname(), PACKAGE, VERSION);
	return 0;
    }

    argc -= optidx;
    argv += optidx;

    if (argc % 2 != 0)
	usage (1);

    for (i = 0; i < argc; i += 2) {
	const char *nodename = argv[i];

	if (strcmp (nodename, "null") == 0)
	    nodename = NULL;

	doit (nodename, argv[i+1]);
    }
    return 0;
}
