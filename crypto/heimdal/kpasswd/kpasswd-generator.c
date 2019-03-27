/*
 * Copyright (c) 2000 - 2004 Kungliga Tekniska Högskolan
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

static unsigned
read_words (const char *filename, char ***ret_w)
{
    unsigned n, alloc;
    FILE *f;
    char buf[256];
    char **w = NULL;

    f = fopen (filename, "r");
    if (f == NULL)
	err (1, "cannot open %s", filename);
    alloc = n = 0;
    while (fgets (buf, sizeof(buf), f) != NULL) {
	buf[strcspn(buf, "\r\n")] = '\0';
	if (n >= alloc) {
	    alloc += 16;
	    w = erealloc (w, alloc * sizeof(char **));
	}
	w[n++] = estrdup (buf);
    }
    *ret_w = w;
    if (n == 0)
	errx(1, "%s is an empty file, no words to try", filename);
    fclose(f);
    return n;
}

static int
nop_prompter (krb5_context context,
	      void *data,
	      const char *name,
	      const char *banner,
	      int num_prompts,
	      krb5_prompt prompts[])
{
    return 0;
}

static void
generate_requests (const char *filename, unsigned nreq)
{
    krb5_context context;
    krb5_error_code ret;
    int i;
    char **words;
    unsigned nwords;

    ret = krb5_init_context (&context);
    if (ret)
	errx (1, "krb5_init_context failed: %d", ret);

    nwords = read_words (filename, &words);

    for (i = 0; i < nreq; ++i) {
	char *name = words[rand() % nwords];
	krb5_get_init_creds_opt *opt;
	krb5_creds cred;
	krb5_principal principal;
	int result_code;
	krb5_data result_code_string, result_string;
	char *old_pwd, *new_pwd;

	krb5_get_init_creds_opt_alloc (context, &opt);
	krb5_get_init_creds_opt_set_tkt_life (opt, 300);
	krb5_get_init_creds_opt_set_forwardable (opt, FALSE);
	krb5_get_init_creds_opt_set_proxiable (opt, FALSE);

	ret = krb5_parse_name (context, name, &principal);
	if (ret)
	    krb5_err (context, 1, ret, "krb5_parse_name %s", name);

	asprintf (&old_pwd, "%s", name);
	asprintf (&new_pwd, "%s2", name);

	ret = krb5_get_init_creds_password (context,
					    &cred,
					    principal,
					    old_pwd,
					    nop_prompter,
					    NULL,
					    0,
					    "kadmin/changepw",
					    opt);
	if( ret == KRB5KRB_AP_ERR_BAD_INTEGRITY
	    || ret == KRB5KRB_AP_ERR_MODIFIED) {
	    char *tmp;

	    tmp = new_pwd;
	    new_pwd = old_pwd;
	    old_pwd = tmp;

	    ret = krb5_get_init_creds_password (context,
						&cred,
						principal,
						old_pwd,
						nop_prompter,
						NULL,
						0,
						"kadmin/changepw",
						opt);
	}
	if (ret)
	    krb5_err (context, 1, ret, "krb5_get_init_creds_password");

	krb5_free_principal (context, principal);


	ret = krb5_set_password (context,
				 &cred,
				 new_pwd,
				 NULL,
				 &result_code,
				 &result_code_string,
				 &result_string);
	if (ret)
	    krb5_err (context, 1, ret, "krb5_change_password");

	free (old_pwd);
	free (new_pwd);
	krb5_free_cred_contents (context, &cred);
	krb5_get_init_creds_opt_free(context, opt);
    }
}

static int version_flag	= 0;
static int help_flag	= 0;

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
		    "file [number]");
    exit (ret);
}

int
main(int argc, char **argv)
{
    int optind = 0;
    int nreq;
    char *end;

    setprogname(argv[0]);
    if(getarg(args, sizeof(args) / sizeof(args[0]), argc, argv, &optind))
	usage(1);
    if (help_flag)
	usage (0);
    if (version_flag) {
	print_version(NULL);
	return 0;
    }
    argc -= optind;
    argv += optind;

    if (argc != 2)
	usage (1);
    srand (0);
    nreq = strtol (argv[1], &end, 0);
    if (argv[1] == end || *end != '\0')
	usage (1);
    generate_requests (argv[0], nreq);
    return 0;
}
