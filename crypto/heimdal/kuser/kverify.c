/*
 * Copyright (c) 1997 - 2005, 2007 Kungliga Tekniska Högskolan
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

static int help_flag = 0;
static int version_flag = 0;

static struct getargs args[] = {
    { "version", 	0,   arg_flag, &version_flag },
    { "help",		0,   arg_flag, &help_flag }
};

static void
usage (int ret)
{
    arg_printusage (args,
		    sizeof(args)/sizeof(*args),
		    NULL,
		    "[principal]");
    exit (ret);
}

int
main(int argc, char **argv)
{
    krb5_context context;
    krb5_error_code ret;
    krb5_creds cred;
    krb5_preauthtype pre_auth_types[] = {KRB5_PADATA_ENC_TIMESTAMP};
    krb5_get_init_creds_opt *get_options;
    krb5_verify_init_creds_opt verify_options;
    krb5_principal principal = NULL;
    int optidx = 0;

    setprogname (argv[0]);

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

    ret = krb5_init_context(&context);
    if (ret)
	errx (1, "krb5_init_context failed: %d", ret);

    ret = krb5_get_init_creds_opt_alloc (context, &get_options);
    if (ret)
	krb5_err(context, 1, ret, "krb5_get_init_creds_opt_alloc");

    krb5_get_init_creds_opt_set_preauth_list (get_options,
					      pre_auth_types,
					      1);

    krb5_verify_init_creds_opt_init (&verify_options);

    if (argc) {
	ret = krb5_parse_name(context, argv[0], &principal);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_parse_name: %s", argv[0]);
    } else {
	ret = krb5_get_default_principal(context, &principal);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_get_default_principal");

    }

    ret = krb5_get_init_creds_password (context,
					&cred,
					principal,
					NULL,
					krb5_prompter_posix,
					NULL,
					0,
					NULL,
					get_options);
    if (ret)
	krb5_err(context, 1, ret,  "krb5_get_init_creds");

    ret = krb5_verify_init_creds (context,
				  &cred,
				  NULL,
				  NULL,
				  NULL,
				  &verify_options);
    if (ret)
	krb5_err(context, 1, ret, "krb5_verify_init_creds");
    krb5_free_cred_contents (context, &cred);
    krb5_free_context (context);
    return 0;
}
