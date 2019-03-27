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

/*
 * Test that removal entry from of empty keytab doesn't corrupts
 * memory.
 */

static void
test_empty_keytab(krb5_context context, const char *keytab)
{
    krb5_error_code ret;
    krb5_keytab id;
    krb5_keytab_entry entry;

    ret = krb5_kt_resolve(context, keytab, &id);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_resolve");

    memset(&entry, 0, sizeof(entry));

    krb5_kt_remove_entry(context, id, &entry);

    ret = krb5_kt_have_content(context, id);
    if (ret == 0)
	krb5_errx(context, 1, "supposed to be empty keytab isn't");

    ret = krb5_kt_close(context, id);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_close");
}

/*
 * Test that memory keytab are refcounted.
 */

static void
test_memory_keytab(krb5_context context, const char *keytab, const char *keytab2)
{
    krb5_error_code ret;
    krb5_keytab id, id2, id3;
    krb5_keytab_entry entry, entry2, entry3;

    ret = krb5_kt_resolve(context, keytab, &id);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_resolve");

    memset(&entry, 0, sizeof(entry));
    ret = krb5_parse_name(context, "lha@SU.SE", &entry.principal);
    if (ret)
	krb5_err(context, 1, ret, "krb5_parse_name");
    entry.vno = 1;
    ret = krb5_generate_random_keyblock(context,
					ETYPE_AES256_CTS_HMAC_SHA1_96,
					&entry.keyblock);
    if (ret)
	krb5_err(context, 1, ret, "krb5_generate_random_keyblock");

    krb5_kt_add_entry(context, id, &entry);

    ret = krb5_kt_resolve(context, keytab, &id2);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_resolve");

    ret = krb5_kt_get_entry(context, id,
			    entry.principal,
			    0,
			    ETYPE_AES256_CTS_HMAC_SHA1_96,
			    &entry2);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_get_entry");
    krb5_kt_free_entry(context, &entry2);

    ret = krb5_kt_close(context, id);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_close");

    ret = krb5_kt_get_entry(context, id2,
			    entry.principal,
			    0,
			    ETYPE_AES256_CTS_HMAC_SHA1_96,
			    &entry2);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_get_entry");
    krb5_kt_free_entry(context, &entry2);

    ret = krb5_kt_close(context, id2);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_close");


    ret = krb5_kt_resolve(context, keytab2, &id3);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_resolve");

    memset(&entry3, 0, sizeof(entry3));
    ret = krb5_parse_name(context, "lha3@SU.SE", &entry3.principal);
    if (ret)
	krb5_err(context, 1, ret, "krb5_parse_name");
    entry3.vno = 1;
    ret = krb5_generate_random_keyblock(context,
					ETYPE_AES256_CTS_HMAC_SHA1_96,
					&entry3.keyblock);
    if (ret)
	krb5_err(context, 1, ret, "krb5_generate_random_keyblock");

    krb5_kt_add_entry(context, id3, &entry3);


    ret = krb5_kt_resolve(context, keytab, &id);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_resolve");

    ret = krb5_kt_get_entry(context, id,
			    entry.principal,
			    0,
			    ETYPE_AES256_CTS_HMAC_SHA1_96,
			    &entry2);
    if (ret == 0)
	krb5_errx(context, 1, "krb5_kt_get_entry when if should fail");

    krb5_kt_remove_entry(context, id, &entry);

    ret = krb5_kt_close(context, id);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_close");

    krb5_kt_free_entry(context, &entry);

    krb5_kt_remove_entry(context, id3, &entry3);

    ret = krb5_kt_close(context, id3);
    if (ret)
	krb5_err(context, 1, ret, "krb5_kt_close");

    krb5_free_principal(context, entry3.principal);
    krb5_free_keyblock_contents(context, &entry3.keyblock);
}

static void
perf_add(krb5_context context, krb5_keytab id, int times)
{
}

static void
perf_find(krb5_context context, krb5_keytab id, int times)
{
}

static void
perf_delete(krb5_context context, krb5_keytab id, int forward, int times)
{
}


static int version_flag = 0;
static int help_flag	= 0;
static char *perf_str   = NULL;
static int times = 1000;

static struct getargs args[] = {
    {"performance",	0,	arg_string,	&perf_str,
     "test performance for named keytab", "keytab" },
    {"times",	0,	arg_integer,	&times,
     "number of times to run the perforamce test", "number" },
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
		    "");
    exit (ret);
}

int
main(int argc, char **argv)
{
    krb5_context context;
    krb5_error_code ret;
    int optidx = 0;

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

    if (argc != 0)
	errx(1, "argc != 0");

    ret = krb5_init_context(&context);
    if (ret)
	errx (1, "krb5_init_context failed: %d", ret);

    if (perf_str) {
	krb5_keytab id;

	ret = krb5_kt_resolve(context, perf_str, &id);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_kt_resolve: %s", perf_str);

	/* add, find, delete on keytab */
	perf_add(context, id, times);
	perf_find(context, id, times);
	perf_delete(context, id, 0, times);

	/* add and find again on used keytab */
	perf_add(context, id, times);
	perf_find(context, id, times);

	ret = krb5_kt_destroy(context, id);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_kt_destroy: %s", perf_str);

	ret = krb5_kt_resolve(context, perf_str, &id);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_kt_resolve: %s", perf_str);

	/* try delete backwards */
#if 0
	perf_add(context, id, times);
	perf_delete(context, id, 1, times);
#endif

	ret = krb5_kt_destroy(context, id);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_kt_destroy");

    } else {

	test_empty_keytab(context, "MEMORY:foo");
	test_empty_keytab(context, "FILE:foo");

	test_memory_keytab(context, "MEMORY:foo", "MEMORY:foo2");

    }

    krb5_free_context(context);

    return 0;
}
