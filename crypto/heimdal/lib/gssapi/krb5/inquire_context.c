/*
 * Copyright (c) 1997, 2003 Kungliga Tekniska Högskolan
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

OM_uint32 GSSAPI_CALLCONV _gsskrb5_inquire_context (
    OM_uint32 * minor_status,
	const gss_ctx_id_t context_handle,
	gss_name_t * src_name,
	gss_name_t * targ_name,
	OM_uint32 * lifetime_rec,
	gss_OID * mech_type,
	OM_uint32 * ctx_flags,
	int * locally_initiated,
	int * open_context
    )
{
    krb5_context context;
    OM_uint32 ret;
    gsskrb5_ctx ctx = (gsskrb5_ctx)context_handle;
    gss_name_t name;

    if (src_name)
	*src_name = GSS_C_NO_NAME;
    if (targ_name)
	*targ_name = GSS_C_NO_NAME;

    GSSAPI_KRB5_INIT (&context);

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);

    if (src_name) {
	name = (gss_name_t)ctx->source;
	ret = _gsskrb5_duplicate_name (minor_status, name, src_name);
	if (ret)
	    goto failed;
    }

    if (targ_name) {
	name = (gss_name_t)ctx->target;
	ret = _gsskrb5_duplicate_name (minor_status, name, targ_name);
	if (ret)
	    goto failed;
    }

    if (lifetime_rec) {
	ret = _gsskrb5_lifetime_left(minor_status,
				     context,
				     ctx->lifetime,
				     lifetime_rec);
	if (ret)
	    goto failed;
    }

    if (mech_type)
	*mech_type = GSS_KRB5_MECHANISM;

    if (ctx_flags)
	*ctx_flags = ctx->flags;

    if (locally_initiated)
	*locally_initiated = ctx->more_flags & LOCAL;

    if (open_context)
	*open_context = ctx->more_flags & OPEN;

    *minor_status = 0;

    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
    return GSS_S_COMPLETE;

failed:
    if (src_name)
	_gsskrb5_release_name(NULL, src_name);
    if (targ_name)
	_gsskrb5_release_name(NULL, targ_name);

    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
    return ret;
}
