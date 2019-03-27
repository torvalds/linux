/*
 * Copyright (c) 1999 - 2003 Kungliga Tekniska Högskolan
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
_gsskrb5_import_sec_context (
    OM_uint32 * minor_status,
    const gss_buffer_t interprocess_token,
    gss_ctx_id_t * context_handle
    )
{
    OM_uint32 ret = GSS_S_FAILURE;
    krb5_context context;
    krb5_error_code kret;
    krb5_storage *sp;
    krb5_auth_context ac;
    krb5_address local, remote;
    krb5_address *localp, *remotep;
    krb5_data data;
    gss_buffer_desc buffer;
    krb5_keyblock keyblock;
    int32_t flags, tmp;
    gsskrb5_ctx ctx;
    gss_name_t name;

    GSSAPI_KRB5_INIT (&context);

    *context_handle = GSS_C_NO_CONTEXT;

    localp = remotep = NULL;

    sp = krb5_storage_from_mem (interprocess_token->value,
				interprocess_token->length);
    if (sp == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
	*minor_status = ENOMEM;
	krb5_storage_free (sp);
	return GSS_S_FAILURE;
    }
    HEIMDAL_MUTEX_init(&ctx->ctx_id_mutex);

    kret = krb5_auth_con_init (context,
			       &ctx->auth_context);
    if (kret) {
	*minor_status = kret;
	ret = GSS_S_FAILURE;
	goto failure;
    }

    /* flags */

    *minor_status = 0;

    if (krb5_ret_int32 (sp, &flags) != 0)
	goto failure;

    /* retrieve the auth context */

    ac = ctx->auth_context;
    if (krb5_ret_int32 (sp, &tmp) != 0)
	goto failure;
    ac->flags = tmp;
    if (flags & SC_LOCAL_ADDRESS) {
	if (krb5_ret_address (sp, localp = &local) != 0)
	    goto failure;
    }

    if (flags & SC_REMOTE_ADDRESS) {
	if (krb5_ret_address (sp, remotep = &remote) != 0)
	    goto failure;
    }

    krb5_auth_con_setaddrs (context, ac, localp, remotep);
    if (localp)
	krb5_free_address (context, localp);
    if (remotep)
	krb5_free_address (context, remotep);
    localp = remotep = NULL;

    if (krb5_ret_int16 (sp, &ac->local_port) != 0)
	goto failure;

    if (krb5_ret_int16 (sp, &ac->remote_port) != 0)
	goto failure;
    if (flags & SC_KEYBLOCK) {
	if (krb5_ret_keyblock (sp, &keyblock) != 0)
	    goto failure;
	krb5_auth_con_setkey (context, ac, &keyblock);
	krb5_free_keyblock_contents (context, &keyblock);
    }
    if (flags & SC_LOCAL_SUBKEY) {
	if (krb5_ret_keyblock (sp, &keyblock) != 0)
	    goto failure;
	krb5_auth_con_setlocalsubkey (context, ac, &keyblock);
	krb5_free_keyblock_contents (context, &keyblock);
    }
    if (flags & SC_REMOTE_SUBKEY) {
	if (krb5_ret_keyblock (sp, &keyblock) != 0)
	    goto failure;
	krb5_auth_con_setremotesubkey (context, ac, &keyblock);
	krb5_free_keyblock_contents (context, &keyblock);
    }
    if (krb5_ret_uint32 (sp, &ac->local_seqnumber))
	goto failure;
    if (krb5_ret_uint32 (sp, &ac->remote_seqnumber))
	goto failure;

    if (krb5_ret_int32 (sp, &tmp) != 0)
	goto failure;
    ac->keytype = tmp;
    if (krb5_ret_int32 (sp, &tmp) != 0)
	goto failure;
    ac->cksumtype = tmp;

    /* names */

    if (krb5_ret_data (sp, &data))
	goto failure;
    buffer.value  = data.data;
    buffer.length = data.length;

    ret = _gsskrb5_import_name (minor_status, &buffer, GSS_C_NT_EXPORT_NAME,
				&name);
    if (ret) {
	ret = _gsskrb5_import_name (minor_status, &buffer, GSS_C_NO_OID,
				    &name);
	if (ret) {
	    krb5_data_free (&data);
	    goto failure;
	}
    }
    ctx->source = (krb5_principal)name;
    krb5_data_free (&data);

    if (krb5_ret_data (sp, &data) != 0)
	goto failure;
    buffer.value  = data.data;
    buffer.length = data.length;

    ret = _gsskrb5_import_name (minor_status, &buffer, GSS_C_NT_EXPORT_NAME,
				&name);
    if (ret) {
	ret = _gsskrb5_import_name (minor_status, &buffer, GSS_C_NO_OID,
				    &name);
	if (ret) {
	    krb5_data_free (&data);
	    goto failure;
	}
    }
    ctx->target = (krb5_principal)name;
    krb5_data_free (&data);

    if (krb5_ret_int32 (sp, &tmp))
	goto failure;
    ctx->flags = tmp;
    if (krb5_ret_int32 (sp, &tmp))
	goto failure;
    ctx->more_flags = tmp;
    if (krb5_ret_int32 (sp, &tmp))
	goto failure;
    ctx->lifetime = tmp;

    ret = _gssapi_msg_order_import(minor_status, sp, &ctx->order);
    if (ret)
        goto failure;

    krb5_storage_free (sp);

    _gsskrb5i_is_cfx(context, ctx, (ctx->more_flags & LOCAL) == 0);

    *context_handle = (gss_ctx_id_t)ctx;

    return GSS_S_COMPLETE;

failure:
    krb5_auth_con_free (context,
			ctx->auth_context);
    if (ctx->source != NULL)
	krb5_free_principal(context, ctx->source);
    if (ctx->target != NULL)
	krb5_free_principal(context, ctx->target);
    if (localp)
	krb5_free_address (context, localp);
    if (remotep)
	krb5_free_address (context, remotep);
    if(ctx->order)
	_gssapi_msg_order_destroy(&ctx->order);
    HEIMDAL_MUTEX_destroy(&ctx->ctx_id_mutex);
    krb5_storage_free (sp);
    free (ctx);
    *context_handle = GSS_C_NO_CONTEXT;
    return ret;
}
