/*
 * Copyright (c) 2008 Kungliga Tekniska Högskolan
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

static int version_flag = 0;
static int help_flag	= 0;

static struct getargs args[] = {
    {"version",	0,	arg_flag,	&version_flag,
     "print version", NULL },
    {"help",	0,	arg_flag,	&help_flag,
     NULL, NULL }
};

static void
usage (int ret)
{
    arg_printusage (args,
		    sizeof(args)/sizeof(*args),
		    NULL,
		    "hostname");
    exit (ret);
}

int
main(int argc, char **argv)
{
    const char *hostname;
    krb5_context context;
    krb5_auth_context ac;
    krb5_error_code ret;
    krb5_creds cred;
    krb5_ccache id;
    krb5_data data;
    int optidx = 0;

    setprogname (argv[0]);

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

    if (argc < 1)
	usage(1);

    hostname = argv[0];

    memset(&cred, 0, sizeof(cred));

    ret = krb5_init_context(&context);
    if (ret)
	errx (1, "krb5_init_context failed: %d", ret);

    ret = krb5_cc_default(context, &id);
    if (ret)
	krb5_err(context, 1, ret, "krb5_cc_default failed");

    ret = krb5_auth_con_init(context, &ac);
    if (ret)
	krb5_err(context, 1, ret, "krb5_auth_con_init failed");

    krb5_auth_con_addflags(context, ac,
			   KRB5_AUTH_CONTEXT_CLEAR_FORWARDED_CRED, NULL);

    ret = krb5_cc_get_principal(context, id, &cred.client);
    if (ret)
	krb5_err(context, 1, ret, "krb5_cc_get_principal");

    ret = krb5_make_principal(context,
			      &cred.server,
			      krb5_principal_get_realm(context, cred.client),
			      KRB5_TGS_NAME,
			      krb5_principal_get_realm(context, cred.client),
			      NULL);
    if (ret)
	krb5_err(context, 1, ret, "krb5_make_principal(server)");

    ret = krb5_get_forwarded_creds (context,
				    ac,
				    id,
				    KDC_OPT_FORWARDABLE,
				    hostname,
				    &cred,
				    &data);
    if (ret)
	krb5_err (context, 1, ret, "krb5_get_forwarded_creds");

    krb5_data_free(&data);
    krb5_free_context(context);

    return 0;
}
