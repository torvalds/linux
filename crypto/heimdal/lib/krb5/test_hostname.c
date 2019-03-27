/*
 * Copyright (c) 2005 Kungliga Tekniska Högskolan
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
 * 3. Neither the name of KTH nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY KTH AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KTH OR ITS CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "krb5_locl.h"
#include <err.h>
#include <getarg.h>

static int debug_flag	= 0;
static int version_flag = 0;
static int help_flag	= 0;

static int
expand_hostname(krb5_context context, const char *host)
{
    krb5_error_code ret;
    char *h, **r;

    ret = krb5_expand_hostname(context, host, &h);
    if (ret)
	krb5_err(context, 1, ret, "krb5_expand_hostname(%s)", host);

    free(h);

    if (debug_flag)
	printf("hostname: %s -> %s\n", host, h);

    ret = krb5_expand_hostname_realms(context, host, &h, &r);
    if (ret)
	krb5_err(context, 1, ret, "krb5_expand_hostname_realms(%s)", host);

    if (debug_flag) {
	int j;

	printf("hostname: %s -> %s\n", host, h);
	for (j = 0; r[j]; j++) {
	    printf("\trealm: %s\n", r[j]);
	}
    }
    free(h);
    krb5_free_host_realm(context, r);

    return 0;
}

static int
test_expand_hostname(krb5_context context)
{
    int i, errors = 0;

    struct t {
	krb5_error_code ret;
	const char *orig_hostname;
	const char *new_hostname;
    } tests[] = {
	{ 0, "pstn1.su.se", "pstn1.su.se" },
	{ 0, "pstnproxy.su.se", "pstnproxy.su.se" },
    };

    for (i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
	errors += expand_hostname(context, tests[i].orig_hostname);
    }

    return errors;
}

static struct getargs args[] = {
    {"debug",	'd',	arg_flag,	&debug_flag,
     "turn on debuggin", NULL },
    {"version",	0,	arg_flag,	&version_flag,
     "print version", NULL },
    {"help",	0,	arg_flag,	&help_flag,
     NULL, NULL }
};

static void
usage (int ret)
{
    arg_printusage (args, sizeof(args)/sizeof(*args), NULL, "hostname ...");
    exit (ret);
}


int
main(int argc, char **argv)
{
    krb5_context context;
    krb5_error_code ret;
    int optidx = 0, errors = 0;

    setprogname(argv[0]);

    if(getarg(args, sizeof(args) / sizeof(args[0]), argc, argv, &optidx))
	usage(1);

    if (help_flag)
	usage (0);

    if(version_flag){
	print_version(NULL);
	exit(0);
    }

    argc -= optidx;
    argv += optidx;

    ret = krb5_init_context(&context);
    if (ret)
	errx (1, "krb5_init_context failed: %d", ret);

    if (argc > 0) {
	while (argc-- > 0)
	    errors += expand_hostname(context, *argv++);
	return errors;
    }

    errors += test_expand_hostname(context);

    krb5_free_context(context);

    return errors;
}
