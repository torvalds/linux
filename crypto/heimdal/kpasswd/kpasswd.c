/*
 * Copyright (c) 1997-2004 Kungliga Tekniska Högskolan
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

#include "kpasswd_locl.h"
RCSID("$Id$");

static int version_flag;
static int help_flag;
static char *admin_principal_str;
static char *cred_cache_str;

static struct getargs args[] = {
    { "admin-principal",	0,   arg_string, &admin_principal_str, NULL,
   	 NULL },
    { "cache",			'c', arg_string, &cred_cache_str, NULL, NULL },
    { "version", 		0,   arg_flag, &version_flag, NULL, NULL },
    { "help",			0,   arg_flag, &help_flag, NULL, NULL }
};

static void
usage (int ret, struct getargs *a, int num_args)
{
    arg_printusage (a, num_args, NULL, "[principal ...]");
    exit (ret);
}

static int
change_password(krb5_context context,
		krb5_principal principal,
		krb5_ccache id)
{
    krb5_data result_code_string, result_string;
    int result_code;
    krb5_error_code ret;
    char pwbuf[BUFSIZ];
    char *msg, *name;

    krb5_data_zero (&result_code_string);
    krb5_data_zero (&result_string);

    name = msg = NULL;
    if (principal == NULL)
	asprintf(&msg, "New password: ");
    else {
	ret = krb5_unparse_name(context, principal, &name);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_unparse_name");

	asprintf(&msg, "New password for %s: ", name);
    }

    if (msg == NULL)
	krb5_errx (context, 1, "out of memory");

    ret = UI_UTIL_read_pw_string (pwbuf, sizeof(pwbuf), msg, 1);
    free(msg);
    if (name)
	free(name);
    if (ret != 0) {
	return 1;
    }

    ret = krb5_set_password_using_ccache (context, id, pwbuf,
					  principal,
					  &result_code,
					  &result_code_string,
					  &result_string);
    if (ret) {
	krb5_warn (context, ret, "krb5_set_password_using_ccache");
	return 1;
    }

    printf ("%s%s%.*s\n", krb5_passwd_result_to_string(context, result_code),
	    result_string.length > 0 ? " : " : "",
	    (int)result_string.length,
	    result_string.length > 0 ? (char *)result_string.data : "");

    krb5_data_free (&result_code_string);
    krb5_data_free (&result_string);

    return ret != 0;
}


int
main (int argc, char **argv)
{
    krb5_error_code ret;
    krb5_context context;
    krb5_principal principal;
    krb5_get_init_creds_opt *opt;
    krb5_ccache id = NULL;
    int exit_value;
    int optidx = 0;

    setprogname(argv[0]);

    if(getarg(args, sizeof(args) / sizeof(args[0]), argc, argv, &optidx))
	usage(1, args, sizeof(args) / sizeof(args[0]));
    if (help_flag)
	usage(0, args, sizeof(args) / sizeof(args[0]));
    if (version_flag) {
	print_version(NULL);
	return 0;
    }
    argc -= optidx;
    argv += optidx;

    ret = krb5_init_context (&context);
    if (ret)
	errx (1, "krb5_init_context failed: %d", ret);

    ret = krb5_get_init_creds_opt_alloc (context, &opt);
    if (ret)
	krb5_err(context, 1, ret, "krb5_get_init_creds_opt_alloc");

    krb5_get_init_creds_opt_set_tkt_life (opt, 300);
    krb5_get_init_creds_opt_set_forwardable (opt, FALSE);
    krb5_get_init_creds_opt_set_proxiable (opt, FALSE);

    if (cred_cache_str) {
	ret = krb5_cc_resolve(context, cred_cache_str, &id);
	if (ret)
	    krb5_err (context, 1, ret, "krb5_cc_resolve");
    } else {
	ret = krb5_cc_new_unique(context, krb5_cc_type_memory, NULL, &id);
	if (ret)
	    krb5_err (context, 1, ret, "krb5_cc_new_unique");
    }

    if (cred_cache_str == NULL) {
	krb5_principal admin_principal = NULL;
	krb5_creds cred;

	if (admin_principal_str) {
	    ret = krb5_parse_name (context, admin_principal_str,
				   &admin_principal);
	    if (ret)
		krb5_err (context, 1, ret, "krb5_parse_name");
	} else if (argc == 1) {
	    ret = krb5_parse_name (context, argv[0], &admin_principal);
	    if (ret)
		krb5_err (context, 1, ret, "krb5_parse_name");
	} else {
	    ret = krb5_get_default_principal (context, &admin_principal);
	    if (ret)
		krb5_err (context, 1, ret, "krb5_get_default_principal");
	}

	ret = krb5_get_init_creds_password (context,
					    &cred,
					    admin_principal,
					    NULL,
					    krb5_prompter_posix,
					    NULL,
					    0,
					    "kadmin/changepw",
					    opt);
	switch (ret) {
	case 0:
	    break;
	case KRB5_LIBOS_PWDINTR :
	    return 1;
	case KRB5KRB_AP_ERR_BAD_INTEGRITY :
	case KRB5KRB_AP_ERR_MODIFIED :
	    krb5_errx(context, 1, "Password incorrect");
	    break;
	default:
	    krb5_err(context, 1, ret, "krb5_get_init_creds");
	}

	krb5_get_init_creds_opt_free(context, opt);

	ret = krb5_cc_initialize(context, id, admin_principal);
	krb5_free_principal(context, admin_principal);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_cc_initialize");

	ret = krb5_cc_store_cred(context, id, &cred);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_cc_store_cred");

	krb5_free_cred_contents (context, &cred);
    }

    if (argc == 0) {
	exit_value = change_password(context, NULL, id);
    } else {
	exit_value = 0;

	while (argc-- > 0) {

	    ret = krb5_parse_name (context, argv[0], &principal);
	    if (ret)
		krb5_err (context, 1, ret, "krb5_parse_name");

	    ret = change_password(context, principal, id);
	    if (ret)
		exit_value = 1;
	    krb5_free_principal(context, principal);
	    argv++;
	}
    }

    if (cred_cache_str == NULL) {
	ret = krb5_cc_destroy(context, id);
	if (ret)
	    krb5_err (context, 1, ret, "krb5_cc_destroy");
    } else {
	ret = krb5_cc_close(context, id);
	if (ret)
	    krb5_err (context, 1, ret, "krb5_cc_close");
    }

    krb5_free_context (context);
    return exit_value;
}
