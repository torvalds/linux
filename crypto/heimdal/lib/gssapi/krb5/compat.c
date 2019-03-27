/*
 * Copyright (c) 2003 - 2005 Kungliga Tekniska Högskolan
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

#include "gsskrb5_locl.h"

static krb5_error_code
check_compat(OM_uint32 *minor_status,
	     krb5_context context, krb5_const_principal name,
	     const char *option, krb5_boolean *compat,
	     krb5_boolean match_val)
{
    krb5_error_code ret = 0;
    char **p, **q;
    krb5_principal match;


    p = krb5_config_get_strings(context, NULL, "gssapi",
				option, NULL);
    if(p == NULL)
	return 0;

    match = NULL;
    for(q = p; *q; q++) {
	ret = krb5_parse_name(context, *q, &match);
	if (ret)
	    break;

	if (krb5_principal_match(context, name, match)) {
	    *compat = match_val;
	    break;
	}

	krb5_free_principal(context, match);
	match = NULL;
    }
    if (match)
	krb5_free_principal(context, match);
    krb5_config_free_strings(p);

    if (ret) {
	if (minor_status)
	    *minor_status = ret;
	return GSS_S_FAILURE;
    }

    return 0;
}

/*
 * ctx->ctx_id_mutex is assumed to be locked
 */

OM_uint32
_gss_DES3_get_mic_compat(OM_uint32 *minor_status,
			 gsskrb5_ctx ctx,
			 krb5_context context)
{
    krb5_boolean use_compat = FALSE;
    OM_uint32 ret;

    if ((ctx->more_flags & COMPAT_OLD_DES3_SELECTED) == 0) {
	ret = check_compat(minor_status, context, ctx->target,
			   "broken_des3_mic", &use_compat, TRUE);
	if (ret)
	    return ret;
	ret = check_compat(minor_status, context, ctx->target,
			   "correct_des3_mic", &use_compat, FALSE);
	if (ret)
	    return ret;

	if (use_compat)
	    ctx->more_flags |= COMPAT_OLD_DES3;
	ctx->more_flags |= COMPAT_OLD_DES3_SELECTED;
    }
    return 0;
}

#if 0
OM_uint32
gss_krb5_compat_des3_mic(OM_uint32 *minor_status, gss_ctx_id_t ctx, int on)
{
    *minor_status = 0;

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);
    if (on) {
	ctx->more_flags |= COMPAT_OLD_DES3;
    } else {
	ctx->more_flags &= ~COMPAT_OLD_DES3;
    }
    ctx->more_flags |= COMPAT_OLD_DES3_SELECTED;
    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);

    return 0;
}
#endif
