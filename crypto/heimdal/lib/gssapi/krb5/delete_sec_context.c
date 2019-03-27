/*
 * Copyright (c) 1997 - 2003 Kungliga Tekniska Högskolan
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

OM_uint32 GSSAPI_CALLCONV
_gsskrb5_delete_sec_context(OM_uint32 * minor_status,
			    gss_ctx_id_t * context_handle,
			    gss_buffer_t output_token)
{
    krb5_context context;
    gsskrb5_ctx ctx;

    GSSAPI_KRB5_INIT (&context);

    *minor_status = 0;

    if (output_token) {
	output_token->length = 0;
	output_token->value  = NULL;
    }

    if (*context_handle == GSS_C_NO_CONTEXT)
	return GSS_S_COMPLETE;

    ctx = (gsskrb5_ctx) *context_handle;
    *context_handle = GSS_C_NO_CONTEXT;

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);

    krb5_auth_con_free (context, ctx->auth_context);
    krb5_auth_con_free (context, ctx->deleg_auth_context);
    if (ctx->kcred)
	krb5_free_creds(context, ctx->kcred);
    if(ctx->source)
	krb5_free_principal (context, ctx->source);
    if(ctx->target)
	krb5_free_principal (context, ctx->target);
    if (ctx->ticket)
	krb5_free_ticket (context, ctx->ticket);
    if(ctx->order)
	_gssapi_msg_order_destroy(&ctx->order);
    if (ctx->service_keyblock)
	krb5_free_keyblock (context, ctx->service_keyblock);
    krb5_data_free(&ctx->fwd_data);
    if (ctx->crypto)
    	krb5_crypto_destroy(context, ctx->crypto);

    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
    HEIMDAL_MUTEX_destroy(&ctx->ctx_id_mutex);
    memset(ctx, 0, sizeof(*ctx));
    free (ctx);
    return GSS_S_COMPLETE;
}
