/*
 * Copyright (c) 2006 - 2008 Kungliga Tekniska Högskolan
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

#include "config.h"

#include <roken.h>
#include <stdio.h>
#include <gssapi.h>
#include <err.h>
#include <getarg.h>
#include "test_common.h"

#include <krb5.h>
#include <heimntlm.h>

static int
test_libntlm_v1(int flags)
{
    const char *user = "foo",
	*domain = "mydomain",
	*password = "digestpassword";
    OM_uint32 maj_stat, min_stat;
    gss_ctx_id_t ctx = GSS_C_NO_CONTEXT;
    gss_buffer_desc input, output;
    struct ntlm_type1 type1;
    struct ntlm_type2 type2;
    struct ntlm_type3 type3;
    struct ntlm_buf data;
    krb5_error_code ret;
    gss_name_t src_name = GSS_C_NO_NAME;

    memset(&type1, 0, sizeof(type1));
    memset(&type2, 0, sizeof(type2));
    memset(&type3, 0, sizeof(type3));

    type1.flags = NTLM_NEG_UNICODE|NTLM_NEG_TARGET|NTLM_NEG_NTLM|flags;
    type1.domain = strdup(domain);
    type1.hostname = NULL;
    type1.os[0] = 0;
    type1.os[1] = 0;

    ret = heim_ntlm_encode_type1(&type1, &data);
    if (ret)
	errx(1, "heim_ntlm_encode_type1");

    input.value = data.data;
    input.length = data.length;

    output.length = 0;
    output.value = NULL;

    maj_stat = gss_accept_sec_context(&min_stat,
				      &ctx,
				      GSS_C_NO_CREDENTIAL,
				      &input,
				      GSS_C_NO_CHANNEL_BINDINGS,
				      NULL,
				      NULL,
				      &output,
				      NULL,
				      NULL,
				      NULL);
    free(data.data);
    if (GSS_ERROR(maj_stat))
	errx(1, "accept_sec_context v1: %s",
	     gssapi_err(maj_stat, min_stat, GSS_C_NO_OID));

    if (output.length == 0)
	errx(1, "output.length == 0");

    data.data = output.value;
    data.length = output.length;

    ret = heim_ntlm_decode_type2(&data, &type2);
    if (ret)
	errx(1, "heim_ntlm_decode_type2");

    gss_release_buffer(&min_stat, &output);

    type3.flags = type2.flags;
    type3.username = rk_UNCONST(user);
    type3.targetname = type2.targetname;
    type3.ws = rk_UNCONST("workstation");

    {
	struct ntlm_buf key;

	heim_ntlm_nt_key(password, &key);

	heim_ntlm_calculate_ntlm1(key.data, key.length,
				  type2.challenge,
				  &type3.ntlm);

	if (flags & NTLM_NEG_KEYEX) {
	    struct ntlm_buf sessionkey;
	    heim_ntlm_build_ntlm1_master(key.data, key.length,
					 &sessionkey,
				     &type3.sessionkey);
	    free(sessionkey.data);
	}
	free(key.data);
    }

    ret = heim_ntlm_encode_type3(&type3, &data);
    if (ret)
	errx(1, "heim_ntlm_encode_type3");

    input.length = data.length;
    input.value = data.data;

    maj_stat = gss_accept_sec_context(&min_stat,
				      &ctx,
				      GSS_C_NO_CREDENTIAL,
				      &input,
				      GSS_C_NO_CHANNEL_BINDINGS,
				      &src_name,
				      NULL,
				      &output,
				      NULL,
				      NULL,
				      NULL);
    free(input.value);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "accept_sec_context v1 2 %s",
	     gssapi_err(maj_stat, min_stat, GSS_C_NO_OID));

    gss_release_buffer(&min_stat, &output);
    gss_delete_sec_context(&min_stat, &ctx, NULL);

    if (src_name == GSS_C_NO_NAME)
	errx(1, "no source name!");

    gss_display_name(&min_stat, src_name, &output, NULL);

    printf("src_name: %.*s\n", (int)output.length, (char*)output.value);

    gss_release_name(&min_stat, &src_name);
    gss_release_buffer(&min_stat, &output);

    return 0;
}

static int
test_libntlm_v2(int flags)
{
    const char *user = "foo",
	*domain = "mydomain",
	*password = "digestpassword";
    OM_uint32 maj_stat, min_stat;
    gss_ctx_id_t ctx = GSS_C_NO_CONTEXT;
    gss_buffer_desc input, output;
    struct ntlm_type1 type1;
    struct ntlm_type2 type2;
    struct ntlm_type3 type3;
    struct ntlm_buf data;
    krb5_error_code ret;

    memset(&type1, 0, sizeof(type1));
    memset(&type2, 0, sizeof(type2));
    memset(&type3, 0, sizeof(type3));

    type1.flags = NTLM_NEG_UNICODE|NTLM_NEG_NTLM|flags;
    type1.domain = strdup(domain);
    type1.hostname = NULL;
    type1.os[0] = 0;
    type1.os[1] = 0;

    ret = heim_ntlm_encode_type1(&type1, &data);
    if (ret)
	errx(1, "heim_ntlm_encode_type1");

    input.value = data.data;
    input.length = data.length;

    output.length = 0;
    output.value = NULL;

    maj_stat = gss_accept_sec_context(&min_stat,
				      &ctx,
				      GSS_C_NO_CREDENTIAL,
				      &input,
				      GSS_C_NO_CHANNEL_BINDINGS,
				      NULL,
				      NULL,
				      &output,
				      NULL,
				      NULL,
				      NULL);
    free(data.data);
    if (GSS_ERROR(maj_stat))
	errx(1, "accept_sec_context v2 %s",
	     gssapi_err(maj_stat, min_stat, GSS_C_NO_OID));

    if (output.length == 0)
	errx(1, "output.length == 0");

    data.data = output.value;
    data.length = output.length;

    ret = heim_ntlm_decode_type2(&data, &type2);
    if (ret)
	errx(1, "heim_ntlm_decode_type2");

    type3.flags = type2.flags;
    type3.username = rk_UNCONST(user);
    type3.targetname = type2.targetname;
    type3.ws = rk_UNCONST("workstation");

    {
	struct ntlm_buf key;
	unsigned char ntlmv2[16];

	heim_ntlm_nt_key(password, &key);

	heim_ntlm_calculate_ntlm2(key.data, key.length,
				  user,
				  type2.targetname,
				  type2.challenge,
				  &type2.targetinfo,
				  ntlmv2,
				  &type3.ntlm);
	free(key.data);

	if (flags & NTLM_NEG_KEYEX) {
	    struct ntlm_buf sessionkey;
	    heim_ntlm_build_ntlm1_master(ntlmv2, sizeof(ntlmv2),
					 &sessionkey,
					 &type3.sessionkey);
	    free(sessionkey.data);
	}
    }

    ret = heim_ntlm_encode_type3(&type3, &data);
    if (ret)
	errx(1, "heim_ntlm_encode_type3");

    input.length = data.length;
    input.value = data.data;

    maj_stat = gss_accept_sec_context(&min_stat,
				      &ctx,
				      GSS_C_NO_CREDENTIAL,
				      &input,
				      GSS_C_NO_CHANNEL_BINDINGS,
				      NULL,
				      NULL,
				      &output,
				      NULL,
				      NULL,
				      NULL);
    free(input.value);
    if (maj_stat != GSS_S_COMPLETE)
	errx(1, "accept_sec_context v2 2 %s",
	     gssapi_err(maj_stat, min_stat, GSS_C_NO_OID));

    gss_delete_sec_context(&min_stat, &ctx, NULL);

    return 0;
}



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
		    NULL, "");
    exit (ret);
}

int
main(int argc, char **argv)
{
    int ret = 0, optind = 0;

    setprogname(argv[0]);

    if(getarg(args, sizeof(args) / sizeof(args[0]), argc, argv, &optind))
	usage(1);

    if (help_flag)
	usage (0);

    if(version_flag){
	print_version(NULL);
	exit(0);
    }

    argc -= optind;
    argv += optind;

    ret += test_libntlm_v1(0);
    ret += test_libntlm_v1(NTLM_NEG_KEYEX);

    ret += test_libntlm_v2(0);
    ret += test_libntlm_v2(NTLM_NEG_KEYEX);

    return 0;
}
