/*
 * Copyright (c) 2003-2004 Kungliga Tekniska Högskolan
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
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <roken.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <gssapi.h>
#include <gssapi_krb5.h>
#include <gssapi_spnego.h>
#include <krb5.h>
#include <err.h>
#include <getarg.h>

static int version_flag = 0;
static int help_flag	= 0;

static void
copy_import(void)
{
    gss_cred_id_t cred1, cred2;
    OM_uint32 maj_stat, min_stat;
    gss_name_t name1, name2;
    OM_uint32 lifetime1, lifetime2;
    gss_cred_usage_t usage1, usage2;
    gss_OID_set mechs1, mechs2;
    krb5_ccache id;
    krb5_error_code ret;
    krb5_context context;
    int equal;

    maj_stat = gss_acquire_cred(&min_stat, GSS_C_NO_NAME, GSS_C_INDEFINITE,
				GSS_C_NO_OID_SET, GSS_C_INITIATE,
				&cred1, NULL, NULL);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_acquire_cred");

    maj_stat = gss_inquire_cred(&min_stat, cred1, &name1, &lifetime1,
				&usage1, &mechs1);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_inquire_cred");

    ret = krb5_init_context(&context);
    if (ret)
	errx(1, "krb5_init_context");

    ret = krb5_cc_new_unique(context, krb5_cc_type_memory, NULL, &id);
    if (ret)
	krb5_err(context, 1, ret, "krb5_cc_new_unique");

    maj_stat = gss_krb5_copy_ccache(&min_stat, cred1, id);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_krb5_copy_ccache");

    maj_stat = gss_krb5_import_cred(&min_stat, id, NULL, NULL, &cred2);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_krb5_import_cred");

    maj_stat = gss_inquire_cred(&min_stat, cred2, &name2, &lifetime2,
				&usage2, &mechs2);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_inquire_cred 2");

    maj_stat = gss_compare_name(&min_stat, name1, name2, &equal);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_compare_name");
    if (!equal)
	errx(1, "names not equal");

    if (lifetime1 != lifetime2)
	errx(1, "lifetime not equal %lu != %lu",
	     (unsigned long)lifetime1, (unsigned long)lifetime2);

    if (usage1 != usage2) {
	/* as long any of them is both are everything it ok */
	if (usage1 != GSS_C_BOTH && usage2 != GSS_C_BOTH)
	    errx(1, "usages disjoined");
    }

    gss_release_name(&min_stat, &name2);
    gss_release_oid_set(&min_stat, &mechs2);

    maj_stat = gss_inquire_cred(&min_stat, cred2, &name2, &lifetime2,
				&usage2, &mechs2);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_inquire_cred");

    maj_stat = gss_compare_name(&min_stat, name1, name2, &equal);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_compare_name");
    if (!equal)
	errx(1, "names not equal");

    if (lifetime1 != lifetime2)
	errx(1, "lifetime not equal %lu != %lu",
	     (unsigned long)lifetime1, (unsigned long)lifetime2);

    gss_release_cred(&min_stat, &cred1);
    gss_release_cred(&min_stat, &cred2);

    gss_release_name(&min_stat, &name1);
    gss_release_name(&min_stat, &name2);

#if 0
    compare(mechs1, mechs2);
#endif

    gss_release_oid_set(&min_stat, &mechs1);
    gss_release_oid_set(&min_stat, &mechs2);

    krb5_cc_destroy(context, id);
    krb5_free_context(context);
}

static struct getargs args[] = {
    {"version",	0,	arg_flag,	&version_flag, "print version", NULL },
    {"help",	0,	arg_flag,	&help_flag,  NULL, NULL }
};

static void
usage (int ret)
{
    arg_printusage (args, sizeof(args)/sizeof(*args),
		    NULL, "");
    exit (ret);
}

int
main(int argc, char **argv)
{
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

    copy_import();

    return 0;
}
