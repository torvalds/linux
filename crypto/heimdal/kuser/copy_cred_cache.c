/*
 * Copyright (c) 2004 Kungliga Tekniska Högskolan
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
#include <config.h>
#include <parse_units.h>
#include <parse_time.h>
#include "kcc-commands.h"

static int32_t
bitswap32(int32_t b)
{
    int32_t r = 0;
    int i;
    for (i = 0; i < 32; i++) {
	r = r << 1 | (b & 1);
	b = b >> 1;
    }
    return r;
}

static void
parse_ticket_flags(krb5_context context,
		   const char *string, krb5_ticket_flags *ret_flags)
{
    TicketFlags ff;
    int flags = parse_flags(string, asn1_TicketFlags_units(), 0);
    if (flags == -1)	/* XXX */
	krb5_errx(context, 1, "bad flags specified: \"%s\"", string);

    memset(&ff, 0, sizeof(ff));
    ff.proxy = 1;
    if ((size_t)parse_flags("proxy", asn1_TicketFlags_units(), 0) == TicketFlags2int(ff))
	ret_flags->i = flags;
    else
	ret_flags->i = bitswap32(flags);
}

struct ctx {
    krb5_flags whichfields;
    krb5_creds mcreds;
};

static krb5_boolean
matchfunc(krb5_context context, void *ptr, const krb5_creds *creds)
{
    struct ctx *ctx = ptr;
    if (krb5_compare_creds(context, ctx->whichfields, &ctx->mcreds, creds))
	return TRUE;
    return FALSE;
}

int
copy_cred_cache(struct copy_cred_cache_options *opt, int argc, char **argv)
{
    krb5_error_code ret;
    const char *from_name, *to_name;
    krb5_ccache from_ccache, to_ccache;
    unsigned int matched;
    struct ctx ctx;

    memset(&ctx, 0, sizeof(ctx));

    if (opt->service_string) {
	ret = krb5_parse_name(kcc_context, opt->service_string, &ctx.mcreds.server);
	if (ret)
	    krb5_err(kcc_context, 1, ret, "%s", opt->service_string);
    }
    if (opt->enctype_string) {
	krb5_enctype enctype;
	ret = krb5_string_to_enctype(kcc_context, opt->enctype_string, &enctype);
	if (ret)
	    krb5_err(kcc_context, 1, ret, "%s", opt->enctype_string);
	ctx.whichfields |= KRB5_TC_MATCH_KEYTYPE;
	ctx.mcreds.session.keytype = enctype;
    }
    if (opt->flags_string) {
	parse_ticket_flags(kcc_context, opt->flags_string, &ctx.mcreds.flags);
	ctx.whichfields |= KRB5_TC_MATCH_FLAGS;
    }
    if (opt->valid_for_string) {
	time_t t = parse_time(opt->valid_for_string, "s");
	if(t < 0)
	    errx(1, "unknown time \"%s\"", opt->valid_for_string);
	ctx.mcreds.times.endtime = time(NULL) + t;
	ctx.whichfields |= KRB5_TC_MATCH_TIMES;
    }
    if (opt->fcache_version_integer)
	krb5_set_fcache_version(kcc_context, opt->fcache_version_integer);

    if (argc == 1) {
	from_name = krb5_cc_default_name(kcc_context);
	to_name = argv[0];
    } else {
	from_name = argv[0];
	to_name = argv[1];
    }

    ret = krb5_cc_resolve(kcc_context, from_name, &from_ccache);
    if (ret)
	krb5_err(kcc_context, 1, ret, "%s", from_name);

    if (opt->krbtgt_only_flag) {
	krb5_principal client;
	ret = krb5_cc_get_principal(kcc_context, from_ccache, &client);
	if (ret)
	    krb5_err(kcc_context, 1, ret, "getting default principal");
	ret = krb5_make_principal(kcc_context, &ctx.mcreds.server,
				  krb5_principal_get_realm(kcc_context, client),
				  KRB5_TGS_NAME,
				  krb5_principal_get_realm(kcc_context, client),
				  NULL);
	if (ret)
	    krb5_err(kcc_context, 1, ret, "constructing krbtgt principal");
	krb5_free_principal(kcc_context, client);
    }
    ret = krb5_cc_resolve(kcc_context, to_name, &to_ccache);
    if (ret)
	krb5_err(kcc_context, 1, ret, "%s", to_name);

    ret = krb5_cc_copy_match_f(kcc_context, from_ccache, to_ccache,
			       matchfunc, &ctx, &matched);
    if (ret)
	krb5_err(kcc_context, 1, ret, "copying cred cache");

    krb5_cc_close(kcc_context, from_ccache);
    if(matched == 0)
	krb5_cc_destroy(kcc_context, to_ccache);
    else
	krb5_cc_close(kcc_context, to_ccache);

    return matched == 0;
}
