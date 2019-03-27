/*
 * Copyright (c) 2003, 2005 Kungliga Tekniska Högskolan
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

#include "kadm5_locl.h"
#include <getarg.h>

RCSID("$Id$");

static int version_flag;
static int help_flag;
static char *principal;
static char *password;

static struct getargs args[] = {
    { "principal", 0, arg_string, &principal },
    { "password", 0, arg_string, &password },
    { "version", 0, arg_flag, &version_flag },
    { "help", 0, arg_flag, &help_flag }
};
int num_args = sizeof(args) / sizeof(args[0]);

int
main(int argc, char **argv)
{
    krb5_error_code ret;
    krb5_context context;
    krb5_principal p;
    const char *s;
    krb5_data pw_data;

    krb5_program_setup(&context, argc, argv, args, num_args, NULL);

    if(help_flag)
	krb5_std_usage(0, args, num_args);
    if(version_flag) {
	print_version(NULL);
	exit(0);
    }

    if (principal == NULL)
	krb5_errx(context, 1, "no principal given");
    if (password == NULL)
	krb5_errx(context, 1, "no password given");

    ret = krb5_parse_name(context, principal, &p);
    if (ret)
	krb5_errx(context, 1, "krb5_parse_name: %s", principal);

    pw_data.data = password;
    pw_data.length = strlen(password);

    kadm5_setup_passwd_quality_check (context, NULL, NULL);
    ret = kadm5_add_passwd_quality_verifier(context, NULL);
    if (ret)
	krb5_errx(context, 1, "kadm5_add_passwd_quality_verifier");

    s = kadm5_check_password_quality (context, p, &pw_data);
    if (s)
	krb5_errx(context, 1, "kadm5_check_password_quality:\n%s", s);

    krb5_free_principal(context, p);
    krb5_free_context(context);

    return 0;
}
