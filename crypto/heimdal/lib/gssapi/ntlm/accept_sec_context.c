/*
 * Copyright (c) 2006 Kungliga Tekniska Högskolan
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

#include "ntlm.h"

/*
 *
 */

OM_uint32
_gss_ntlm_allocate_ctx(OM_uint32 *minor_status, ntlm_ctx *ctx)
{
    OM_uint32 maj_stat;
    struct ntlm_server_interface *ns_interface = NULL;

#ifdef DIGEST
    ns_interface = &ntlmsspi_kdc_digest;
#endif
    if (ns_interface == NULL)
	return GSS_S_FAILURE;

    *ctx = calloc(1, sizeof(**ctx));

    (*ctx)->server = ns_interface;

    maj_stat = (*(*ctx)->server->nsi_init)(minor_status, &(*ctx)->ictx);
    if (maj_stat != GSS_S_COMPLETE)
	return maj_stat;

    return GSS_S_COMPLETE;
}

/*
 *
 */

OM_uint32 GSSAPI_CALLCONV
_gss_ntlm_accept_sec_context
(OM_uint32 * minor_status,
 gss_ctx_id_t * context_handle,
 const gss_cred_id_t acceptor_cred_handle,
 const gss_buffer_t input_token_buffer,
 const gss_channel_bindings_t input_chan_bindings,
 gss_name_t * src_name,
 gss_OID * mech_type,
 gss_buffer_t output_token,
 OM_uint32 * ret_flags,
 OM_uint32 * time_rec,
 gss_cred_id_t * delegated_cred_handle
    )
{
    krb5_error_code ret;
    struct ntlm_buf data;
    OM_uint32 junk;
    ntlm_ctx ctx;

    output_token->value = NULL;
    output_token->length = 0;

    *minor_status = 0;

    if (context_handle == NULL)
	return GSS_S_FAILURE;

    if (input_token_buffer == GSS_C_NO_BUFFER)
	return GSS_S_FAILURE;

    if (src_name)
	*src_name = GSS_C_NO_NAME;
    if (mech_type)
	*mech_type = GSS_C_NO_OID;
    if (ret_flags)
	*ret_flags = 0;
    if (time_rec)
	*time_rec = 0;
    if (delegated_cred_handle)
	*delegated_cred_handle = GSS_C_NO_CREDENTIAL;

    if (*context_handle == GSS_C_NO_CONTEXT) {
	struct ntlm_type1 type1;
	OM_uint32 major_status;
	OM_uint32 retflags;
	struct ntlm_buf out;

	major_status = _gss_ntlm_allocate_ctx(minor_status, &ctx);
	if (major_status)
	    return major_status;
	*context_handle = (gss_ctx_id_t)ctx;

	/* check if the mechs is allowed by remote service */
	major_status = (*ctx->server->nsi_probe)(minor_status, ctx->ictx, NULL);
	if (major_status) {
	    _gss_ntlm_delete_sec_context(minor_status, context_handle, NULL);
	    return major_status;
	}

	data.data = input_token_buffer->value;
	data.length = input_token_buffer->length;

	ret = heim_ntlm_decode_type1(&data, &type1);
	if (ret) {
	    _gss_ntlm_delete_sec_context(minor_status, context_handle, NULL);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	if ((type1.flags & NTLM_NEG_UNICODE) == 0) {
	    heim_ntlm_free_type1(&type1);
	    _gss_ntlm_delete_sec_context(minor_status, context_handle, NULL);
	    *minor_status = EINVAL;
	    return GSS_S_FAILURE;
	}

	if (type1.flags & NTLM_NEG_SIGN)
	    ctx->gssflags |= GSS_C_CONF_FLAG;
	if (type1.flags & NTLM_NEG_SIGN)
	    ctx->gssflags |= GSS_C_INTEG_FLAG;

	major_status = (*ctx->server->nsi_type2)(minor_status,
						 ctx->ictx,
						 type1.flags,
						 type1.hostname,
						 type1.domain,
						 &retflags,
						 &out);
	heim_ntlm_free_type1(&type1);
	if (major_status != GSS_S_COMPLETE) {
	    OM_uint32 gunk;
	    _gss_ntlm_delete_sec_context(&gunk, context_handle, NULL);
	    return major_status;
	}

	output_token->value = malloc(out.length);
	if (output_token->value == NULL && out.length != 0) {
	    OM_uint32 gunk;
	    _gss_ntlm_delete_sec_context(&gunk, context_handle, NULL);
	    *minor_status = ENOMEM;
	    return GSS_S_FAILURE;
	}
	memcpy(output_token->value, out.data, out.length);
	output_token->length = out.length;

	ctx->flags = retflags;

	return GSS_S_CONTINUE_NEEDED;
    } else {
	OM_uint32 maj_stat;
	struct ntlm_type3 type3;
	struct ntlm_buf session;

	ctx = (ntlm_ctx)*context_handle;

	data.data = input_token_buffer->value;
	data.length = input_token_buffer->length;

	ret = heim_ntlm_decode_type3(&data, 1, &type3);
	if (ret) {
	    _gss_ntlm_delete_sec_context(minor_status, context_handle, NULL);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	maj_stat = (*ctx->server->nsi_type3)(minor_status,
					     ctx->ictx,
					     &type3,
					     &session);
	if (maj_stat) {
	    heim_ntlm_free_type3(&type3);
	    _gss_ntlm_delete_sec_context(minor_status, context_handle, NULL);
	    return maj_stat;
	}

	if (src_name) {
	    ntlm_name n = calloc(1, sizeof(*n));
	    if (n) {
		n->user = strdup(type3.username);
		n->domain = strdup(type3.targetname);
	    }
	    if (n == NULL || n->user == NULL || n->domain == NULL) {
		gss_name_t tempn =  (gss_name_t)n;
		_gss_ntlm_release_name(&junk, &tempn);
		heim_ntlm_free_type3(&type3);
		_gss_ntlm_delete_sec_context(minor_status,
					     context_handle, NULL);
		return maj_stat;
	    }
	    *src_name = (gss_name_t)n;
	}

	heim_ntlm_free_type3(&type3);

	ret = krb5_data_copy(&ctx->sessionkey,
			     session.data, session.length);
	if (ret) {
	    if (src_name)
		_gss_ntlm_release_name(&junk, src_name);
	    _gss_ntlm_delete_sec_context(minor_status, context_handle, NULL);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	if (session.length != 0) {

	    ctx->status |= STATUS_SESSIONKEY;

	    if (ctx->flags & NTLM_NEG_NTLM2_SESSION) {
		_gss_ntlm_set_key(&ctx->u.v2.send, 1,
				  (ctx->flags & NTLM_NEG_KEYEX),
				  ctx->sessionkey.data,
				  ctx->sessionkey.length);
		_gss_ntlm_set_key(&ctx->u.v2.recv, 0,
				  (ctx->flags & NTLM_NEG_KEYEX),
				  ctx->sessionkey.data,
				  ctx->sessionkey.length);
	    } else {
		RC4_set_key(&ctx->u.v1.crypto_send.key,
			    ctx->sessionkey.length,
			    ctx->sessionkey.data);
		RC4_set_key(&ctx->u.v1.crypto_recv.key,
			    ctx->sessionkey.length,
			    ctx->sessionkey.data);
	    }
	}

	if (mech_type)
	    *mech_type = GSS_NTLM_MECHANISM;
	if (time_rec)
	    *time_rec = GSS_C_INDEFINITE;

	ctx->status |= STATUS_OPEN;

	if (ret_flags)
	    *ret_flags = ctx->gssflags;

	return GSS_S_COMPLETE;
    }
}
