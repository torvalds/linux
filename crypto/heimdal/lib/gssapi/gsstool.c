/*
 * Copyright (c) 2006 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 - 2010 Apple Inc. All rights reserved.
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

#include <config.h>
#include <roken.h>

#include <stdio.h>
#include <gssapi.h>
#include <gssapi_krb5.h>
#include <gssapi_spnego.h>
#include <gssapi_ntlm.h>
#include <err.h>
#include <getarg.h>
#include <rtbl.h>
#include <gss-commands.h>


static int version_flag = 0;
static int help_flag	= 0;

static struct getargs args[] = {
    {"version",	0,	arg_flag,	&version_flag, "print version", NULL },
    {"help",	0,	arg_flag,	&help_flag,  NULL, NULL }
};

static void
usage (int ret)
{
    arg_printusage (args, sizeof(args)/sizeof(*args),
		    NULL, "service@host");
    exit (ret);
}

#define COL_OID		"OID"
#define COL_NAME	"Name"
#define COL_DESC	"Description"
#define COL_VALUE	"Value"
#define COL_MECH	"Mech"
#define COL_EXPIRE	"Expire"
#define COL_SASL	"SASL"

int
supported_mechanisms(void *argptr, int argc, char **argv)
{
    OM_uint32 maj_stat, min_stat;
    gss_OID_set mechs;
    rtbl_t ct;
    size_t i;

    maj_stat = gss_indicate_mechs(&min_stat, &mechs);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "gss_indicate_mechs failed");

    printf("Supported mechanisms:\n");

    ct = rtbl_create();
    if (ct == NULL)
	errx(1, "rtbl_create");

    rtbl_set_separator(ct, "  ");
    rtbl_add_column(ct, COL_OID, 0);
    rtbl_add_column(ct, COL_NAME, 0);
    rtbl_add_column(ct, COL_DESC, 0);
    rtbl_add_column(ct, COL_SASL, 0);

    for (i = 0; i < mechs->count; i++) {
	gss_buffer_desc str, sasl_name, mech_name, mech_desc;

	maj_stat = gss_oid_to_str(&min_stat, &mechs->elements[i], &str);
	if (maj_stat != GSS_S_COMPLETE)
	    errx(1, "gss_oid_to_str failed");

	rtbl_add_column_entryv(ct, COL_OID, "%.*s",
			       (int)str.length, (char *)str.value);
	gss_release_buffer(&min_stat, &str);

	(void)gss_inquire_saslname_for_mech(&min_stat,
					    &mechs->elements[i],
					    &sasl_name,
					    &mech_name,
					    &mech_desc);

	rtbl_add_column_entryv(ct, COL_NAME, "%.*s",
			       (int)mech_name.length, (char *)mech_name.value);
	rtbl_add_column_entryv(ct, COL_DESC, "%.*s",
			       (int)mech_desc.length, (char *)mech_desc.value);
	rtbl_add_column_entryv(ct, COL_SASL, "%.*s",
			       (int)sasl_name.length, (char *)sasl_name.value);

	gss_release_buffer(&min_stat, &mech_name);
	gss_release_buffer(&min_stat, &mech_desc);
	gss_release_buffer(&min_stat, &sasl_name);

    }
    gss_release_oid_set(&min_stat, &mechs);

    rtbl_format(ct, stdout);
    rtbl_destroy(ct);

    return 0;
}

static void
print_mech_attr(const char *mechname, gss_const_OID mech, gss_OID_set set)
{
    gss_buffer_desc name, desc;
    OM_uint32 major, minor;
    rtbl_t ct;
    size_t n;

    ct = rtbl_create();
    if (ct == NULL)
	errx(1, "rtbl_create");

    rtbl_set_separator(ct, "  ");
    rtbl_add_column(ct, COL_OID, 0);
    rtbl_add_column(ct, COL_DESC, 0);
    if (mech)
	rtbl_add_column(ct, COL_VALUE, 0);

    for (n = 0; n < set->count; n++) {
	major = gss_display_mech_attr(&minor, &set->elements[n], &name, &desc, NULL);
	if (major)
	    continue;

	rtbl_add_column_entryv(ct, COL_OID, "%.*s",
			       (int)name.length, (char *)name.value);
	rtbl_add_column_entryv(ct, COL_DESC, "%.*s",
			       (int)desc.length, (char *)desc.value);
	if (mech) {
	    gss_buffer_desc value;

	    if (gss_mo_get(mech, &set->elements[n], &value) != 0)
		value.length = 0;

	    if (value.length)
		rtbl_add_column_entryv(ct, COL_VALUE, "%.*s",
				       (int)value.length, (char *)value.value);
	    else
		rtbl_add_column_entryv(ct, COL_VALUE, "<>");
	    gss_release_buffer(&minor, &value);
	}

	gss_release_buffer(&minor, &name);
	gss_release_buffer(&minor, &desc);
    }

    printf("attributes for: %s\n", mechname);
    rtbl_format(ct, stdout);
    rtbl_destroy(ct);
}


int
attrs_for_mech(struct attrs_for_mech_options *opt, int argc, char **argv)
{
    gss_OID_set mech_attr = NULL, known_mech_attrs = NULL;
    gss_OID mech = GSS_C_NO_OID;
    OM_uint32 major, minor;

    if (opt->mech_string) {
	mech = gss_name_to_oid(opt->mech_string);
	if (mech == NULL)
	    errx(1, "mech %s is unknown", opt->mech_string);
    }

    major = gss_inquire_attrs_for_mech(&minor, mech, &mech_attr, &known_mech_attrs);
    if (major)
	errx(1, "gss_inquire_attrs_for_mech");

    if (mech) {
	print_mech_attr(opt->mech_string, mech, mech_attr);
    }

    if (opt->all_flag) {
	print_mech_attr("all mechs", NULL, known_mech_attrs);
    }

    gss_release_oid_set(&minor, &mech_attr);
    gss_release_oid_set(&minor, &known_mech_attrs);

    return 0;
}


/*
 *
 */

int
help(void *opt, int argc, char **argv)
{
    sl_slc_help(commands, argc, argv);
    return 0;
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

    if (argc == 0) {
	help(NULL, argc, argv);
	return 1;
    }

    return sl_command (commands, argc, argv);
}
