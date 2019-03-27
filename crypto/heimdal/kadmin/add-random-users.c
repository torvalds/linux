/*
 * Copyright (c) 2000 - 2001 Kungliga Tekniska Högskolan
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

#include "kadmin_locl.h"

#define WORDS_FILENAME "/usr/share/dict/words"

#define NUSERS 1000

#define WORDBUF_SIZE 65535

static unsigned
read_words (const char *filename, char ***ret_w)
{
    unsigned n, alloc;
    FILE *f;
    char buf[256];
    char **w = NULL;
    char *wbuf = NULL, *wptr = NULL, *wend = NULL;

    f = fopen (filename, "r");
    if (f == NULL)
	err (1, "cannot open %s", filename);
    alloc = n = 0;
    while (fgets (buf, sizeof(buf), f) != NULL) {
	size_t len;

	buf[strcspn(buf, "\r\n")] = '\0';
	if (n >= alloc) {
	    alloc = max(alloc + 16, alloc * 2);
	    w = erealloc (w, alloc * sizeof(char **));
	}
	len = strlen(buf);
	if (wptr + len + 1 >= wend) {
	    wptr = wbuf = emalloc (WORDBUF_SIZE);
	    wend = wbuf + WORDBUF_SIZE;
	}
	memmove (wptr, buf, len + 1);
	w[n++] = wptr;
	wptr += len + 1;
    }
    if (n == 0)
	errx(1, "%s is an empty file, no words to try", filename);
    *ret_w = w;
    fclose(f);
    return n;
}

static void
add_user (krb5_context context, void *kadm_handle,
	  unsigned nwords, char **words)
{
    kadm5_principal_ent_rec princ;
    char name[64];
    int r1, r2;
    krb5_error_code ret;
    int mask;

    r1 = rand();
    r2 = rand();

    snprintf (name, sizeof(name), "%s%d", words[r1 % nwords], r2 % 1000);

    mask = KADM5_PRINCIPAL;

    memset(&princ, 0, sizeof(princ));
    ret = krb5_parse_name(context, name, &princ.principal);
    if (ret)
	krb5_err(context, 1, ret, "krb5_parse_name");

    ret = kadm5_create_principal (kadm_handle, &princ, mask, name);
    if (ret)
	krb5_err (context, 1, ret, "kadm5_create_principal");
    kadm5_free_principal_ent(kadm_handle, &princ);
    printf ("%s\n", name);
}

static void
add_users (const char *filename, unsigned n)
{
    krb5_error_code ret;
    int i;
    void *kadm_handle;
    krb5_context context;
    unsigned nwords;
    char **words;

    ret = krb5_init_context(&context);
    if (ret)
	errx (1, "krb5_init_context failed: %d", ret);
    ret = kadm5_s_init_with_password_ctx(context,
					 KADM5_ADMIN_SERVICE,
					 NULL,
					 KADM5_ADMIN_SERVICE,
					 NULL, 0, 0,
					 &kadm_handle);
    if(ret)
	krb5_err(context, 1, ret, "kadm5_init_with_password");

    nwords = read_words (filename, &words);

    for (i = 0; i < n; ++i)
	add_user (context, kadm_handle, nwords, words);
    kadm5_destroy(kadm_handle);
    krb5_free_context(context);
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
		    "[filename [n]]");
    exit (ret);
}

int
main(int argc, char **argv)
{
    int optidx = 0;
    int n = NUSERS;
    const char *filename = WORDS_FILENAME;

    setprogname(argv[0]);
    if(getarg(args, sizeof(args) / sizeof(args[0]), argc, argv, &optidx))
	usage(1);
    if (help_flag)
	usage (0);
    if (version_flag) {
	print_version(NULL);
	return 0;
    }
    srand (0);
    argc -= optidx;
    argv += optidx;

    if (argc > 0) {
	if (argc > 1)
	    n = atoi(argv[1]);
	filename = argv[0];
    }

    add_users (filename, n);
    return 0;
}
