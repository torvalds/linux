/*
 * Copyright (c) 1997 - 2000, 2003 Kungliga Tekniska Högskolan
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

#include "kuser_locl.h"

static const char *cache;
static const char *credential;
static int help_flag;
static int version_flag;
#ifndef NO_AFS
static int unlog_flag = 1;
#endif
static int dest_tkt_flag = 1;
static int all_flag = 0;

struct getargs args[] = {
    { "credential",	0,   arg_string, rk_UNCONST(&credential),
      "remove one credential", "principal" },
    { "cache",		'c', arg_string, rk_UNCONST(&cache), "cache to destroy", "cache" },
    { "all",		'A', arg_flag, &all_flag, "destroy all caches", NULL },
#ifndef NO_AFS
    { "unlog",		0,   arg_negative_flag, &unlog_flag,
      "do not destroy tokens", NULL },
#endif
    { "delete-v4",	0,   arg_negative_flag, &dest_tkt_flag,
      "do not destroy v4 tickets", NULL },
    { "version", 	0,   arg_flag, &version_flag, NULL, NULL },
    { "help",		'h', arg_flag, &help_flag, NULL, NULL}
};

int num_args = sizeof(args) / sizeof(args[0]);

static void
usage (int status)
{
    arg_printusage (args, num_args, NULL, "");
    exit (status);
}

int
main (int argc, char **argv)
{
    krb5_error_code ret;
    krb5_context context;
    krb5_ccache  ccache;
    int optidx = 0;
    int exit_val = 0;

    setprogname (argv[0]);

    if(getarg(args, num_args, argc, argv, &optidx))
	usage(1);

    if (help_flag)
	usage (0);

    if(version_flag){
	print_version(NULL);
	exit(0);
    }

    argc -= optidx;
    argv += optidx;

    if (argc != 0)
	usage (1);

    ret = krb5_init_context (&context);
    if (ret)
	errx (1, "krb5_init_context failed: %d", ret);

    if (all_flag) {
	krb5_cccol_cursor cursor;

	ret = krb5_cccol_cursor_new (context, &cursor);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_cccol_cursor_new");

	while (krb5_cccol_cursor_next (context, cursor, &ccache) == 0 && ccache != NULL) {

	    ret = krb5_cc_destroy (context, ccache);
	    if (ret) {
		krb5_warn(context, ret, "krb5_cc_destroy");
		exit_val = 1;
	    }
	}
	krb5_cccol_cursor_free(context, &cursor);

    } else {
	if(cache == NULL) {
	    ret = krb5_cc_default(context, &ccache);
	    if (ret)
		krb5_err(context, 1, ret, "krb5_cc_default");
	} else {
	    ret =  krb5_cc_resolve(context,
				   cache,
				   &ccache);
	    if (ret)
		krb5_err(context, 1, ret, "krb5_cc_resolve");
	}

	if (ret == 0) {
	    if (credential) {
		krb5_creds mcred;

		krb5_cc_clear_mcred(&mcred);

		ret = krb5_parse_name(context, credential, &mcred.server);
		if (ret)
		    krb5_err(context, 1, ret,
			     "Can't parse principal %s", credential);

		ret = krb5_cc_remove_cred(context, ccache, 0, &mcred);
		if (ret)
		    krb5_err(context, 1, ret,
			     "Failed to remove principal %s", credential);

		krb5_cc_close(context, ccache);
		krb5_free_principal(context, mcred.server);
		krb5_free_context(context);
		return 0;
	    }

	    ret = krb5_cc_destroy (context, ccache);
	    if (ret) {
		krb5_warn(context, ret, "krb5_cc_destroy");
		exit_val = 1;
	    }
	}
    }

    krb5_free_context (context);

#ifndef NO_AFS
    if (unlog_flag && k_hasafs ()) {
	if (k_unlog ())
	    exit_val = 1;
    }
#endif

    return exit_val;
}
