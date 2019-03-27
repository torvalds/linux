/*
 * Copyright (c) 1997 - 2008 Kungliga Tekniska Högskolan
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

static char *cache_str;
static char *out_cache_str;
static char *delegation_cred_str;
static char *etype_str;
static int transit_flag = 1;
static int forwardable_flag;
static int canonicalize_flag;
static char *impersonate_str;
static char *nametype_str;
static int version_flag;
static int help_flag;

struct getargs args[] = {
    { "cache",		'c', arg_string, &cache_str,
      NP_("credential cache to use", ""), "cache"},
    { "out-cache",	0,   arg_string, &out_cache_str,
      NP_("credential cache to store credential in", ""), "cache"},
    { "delegation-credential-cache",0,arg_string, &delegation_cred_str,
      NP_("where to find the ticket use for delegation", ""), "cache"},
    { "canonicalize",	0, arg_flag, &canonicalize_flag,
      NP_("canonicalize the principal", ""), NULL },
    { "forwardable",	0, arg_flag, &forwardable_flag,
      NP_("forwardable ticket requested", ""), NULL},
    { "transit-check",	0,   arg_negative_flag, &transit_flag, NULL, NULL },
    { "enctype",	'e', arg_string, &etype_str,
      NP_("encryption type to use", ""), "enctype"},
    { "impersonate",	0,   arg_string, &impersonate_str,
      NP_("client to impersonate", ""), "principal"},
    { "name-type",		0,   arg_string, &nametype_str, NULL, NULL },
    { "version", 	0,   arg_flag, &version_flag, NULL, NULL },
    { "help",		0,   arg_flag, &help_flag, NULL, NULL }
};

static void
usage (int ret)
{
    arg_printusage (args,
		    sizeof(args)/sizeof(*args),
		    NULL,
		    "service");
    exit (ret);
}

int
main(int argc, char **argv)
{
    krb5_error_code ret;
    krb5_context context;
    krb5_ccache cache;
    krb5_creds *out;
    int optidx = 0;
    krb5_get_creds_opt opt;
    krb5_principal server;
    krb5_principal impersonate = NULL;

    setprogname (argv[0]);

    ret = krb5_init_context (&context);
    if (ret)
	errx(1, "krb5_init_context failed: %d", ret);

    if(getarg(args, sizeof(args) / sizeof(args[0]), argc, argv, &optidx))
	usage(1);

    if (help_flag)
	usage (0);

    if(version_flag) {
	print_version(NULL);
	exit(0);
    }

    argc -= optidx;
    argv += optidx;

    if (argc != 1)
	usage (1);

    if(cache_str) {
	ret = krb5_cc_resolve(context, cache_str, &cache);
	if (ret)
	    krb5_err (context, 1, ret, "%s", cache_str);
    } else {
	ret = krb5_cc_default (context, &cache);
	if (ret)
	    krb5_err (context, 1, ret, "krb5_cc_resolve");
    }

    ret = krb5_get_creds_opt_alloc(context, &opt);
    if (ret)
	krb5_err (context, 1, ret, "krb5_get_creds_opt_alloc");

    if (etype_str) {
	krb5_enctype enctype;

	ret = krb5_string_to_enctype(context, etype_str, &enctype);
	if (ret)
	    krb5_errx (context, 1, N_("unrecognized enctype: %s", ""),
		       etype_str);
	krb5_get_creds_opt_set_enctype(context, opt, enctype);
    }

    if (impersonate_str) {
	ret = krb5_parse_name(context, impersonate_str, &impersonate);
	if (ret)
	    krb5_err (context, 1, ret, "krb5_parse_name %s", impersonate_str);
	krb5_get_creds_opt_set_impersonate(context, opt, impersonate);
	krb5_get_creds_opt_add_options(context, opt, KRB5_GC_NO_STORE);
    }

    if (out_cache_str)
	krb5_get_creds_opt_add_options(context, opt, KRB5_GC_NO_STORE);

    if (forwardable_flag)
	krb5_get_creds_opt_add_options(context, opt, KRB5_GC_FORWARDABLE);
    if (!transit_flag)
	krb5_get_creds_opt_add_options(context, opt, KRB5_GC_NO_TRANSIT_CHECK);
    if (canonicalize_flag)
	krb5_get_creds_opt_add_options(context, opt, KRB5_GC_CANONICALIZE);

    if (delegation_cred_str) {
	krb5_ccache id;
	krb5_creds c, mc;
	Ticket ticket;

	krb5_cc_clear_mcred(&mc);
	ret = krb5_cc_get_principal(context, cache, &mc.server);
	if (ret)
	    krb5_err (context, 1, ret, "krb5_cc_get_principal");

	ret = krb5_cc_resolve(context, delegation_cred_str, &id);
	if(ret)
	    krb5_err (context, 1, ret, "krb5_cc_resolve");

	ret = krb5_cc_retrieve_cred(context, id, 0, &mc, &c);
	if(ret)
	    krb5_err (context, 1, ret, "krb5_cc_retrieve_cred");

	ret = decode_Ticket(c.ticket.data, c.ticket.length, &ticket, NULL);
	if (ret) {
	    krb5_clear_error_message(context);
	    krb5_err (context, 1, ret, "decode_Ticket");
	}
	krb5_free_cred_contents(context, &c);

	ret = krb5_get_creds_opt_set_ticket(context, opt, &ticket);
	if(ret)
	    krb5_err (context, 1, ret, "krb5_get_creds_opt_set_ticket");
	free_Ticket(&ticket);

	krb5_cc_close (context, id);
	krb5_free_principal(context, mc.server);

	krb5_get_creds_opt_add_options(context, opt,
				       KRB5_GC_CONSTRAINED_DELEGATION);
    }

    ret = krb5_parse_name(context, argv[0], &server);
    if (ret)
	krb5_err (context, 1, ret, "krb5_parse_name %s", argv[0]);

    if (nametype_str) {
	int32_t nametype;

	ret = krb5_parse_nametype(context, nametype_str, &nametype);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_parse_nametype");

	server->name.name_type = (NAME_TYPE)nametype;
    }

    ret = krb5_get_creds(context, opt, cache, server, &out);
    if (ret)
	krb5_err (context, 1, ret, "krb5_get_creds");

    if (out_cache_str) {
	krb5_ccache id;

	ret = krb5_cc_resolve(context, out_cache_str, &id);
	if(ret)
	    krb5_err (context, 1, ret, "krb5_cc_resolve");

	ret = krb5_cc_initialize(context, id, out->client);
	if(ret)
	    krb5_err (context, 1, ret, "krb5_cc_initialize");

	ret = krb5_cc_store_cred(context, id, out);
	if(ret)
	    krb5_err (context, 1, ret, "krb5_cc_store_cred");
	krb5_cc_close (context, id);
    }

    krb5_free_creds(context, out);
    krb5_free_principal(context, server);
    krb5_get_creds_opt_free(context, opt);
    krb5_cc_close (context, cache);
    krb5_free_context (context);

    return 0;
}
