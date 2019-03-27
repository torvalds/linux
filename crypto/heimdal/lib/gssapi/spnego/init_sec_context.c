/*
 * Copyright (c) 1997 - 2004 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * Portions Copyright (c) 2004 PADL Software Pty Ltd.
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

#include "spnego_locl.h"

/*
 * Is target_name an sane target for `mech´.
 */

static OM_uint32
initiator_approved(gss_name_t target_name, gss_OID mech)
{
    OM_uint32 min_stat, maj_stat;
    gss_ctx_id_t ctx = GSS_C_NO_CONTEXT;
    gss_buffer_desc out;

    maj_stat = gss_init_sec_context(&min_stat,
				    GSS_C_NO_CREDENTIAL,
				    &ctx,
				    target_name,
				    mech,
				    0,
				    GSS_C_INDEFINITE,
				    GSS_C_NO_CHANNEL_BINDINGS,
				    GSS_C_NO_BUFFER,
				    NULL,
				    &out,
				    NULL,
				    NULL);
    if (GSS_ERROR(maj_stat)) {
	gss_mg_collect_error(mech, maj_stat, min_stat);
	return GSS_S_BAD_MECH;
    }
    gss_release_buffer(&min_stat, &out);
    gss_delete_sec_context(&min_stat, &ctx, NULL);

    return GSS_S_COMPLETE;
}

/*
 * Send a reply. Note that we only need to send a reply if we
 * need to send a MIC or a mechanism token. Otherwise, we can
 * return an empty buffer.
 *
 * The return value of this will be returned to the API, so it
 * must return GSS_S_CONTINUE_NEEDED if a token was generated.
 */
static OM_uint32
spnego_reply_internal(OM_uint32 *minor_status,
		      gssspnego_ctx context_handle,
		      const gss_buffer_t mech_buf,
		      gss_buffer_t mech_token,
		      gss_buffer_t output_token)
{
    NegotiationToken nt;
    gss_buffer_desc mic_buf;
    OM_uint32 ret;
    size_t size;

    if (mech_buf == GSS_C_NO_BUFFER && mech_token->length == 0) {
	output_token->length = 0;
	output_token->value = NULL;

	return context_handle->open ? GSS_S_COMPLETE : GSS_S_FAILURE;
    }

    memset(&nt, 0, sizeof(nt));

    nt.element = choice_NegotiationToken_negTokenResp;

    ALLOC(nt.u.negTokenResp.negResult, 1);
    if (nt.u.negTokenResp.negResult == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    nt.u.negTokenResp.supportedMech = NULL;

    output_token->length = 0;
    output_token->value = NULL;

    if (mech_token->length == 0) {
	nt.u.negTokenResp.responseToken = NULL;
	*(nt.u.negTokenResp.negResult)  = accept_completed;
    } else {
	ALLOC(nt.u.negTokenResp.responseToken, 1);
	if (nt.u.negTokenResp.responseToken == NULL) {
	    free_NegotiationToken(&nt);
	    *minor_status = ENOMEM;
	    return GSS_S_FAILURE;
	}
	nt.u.negTokenResp.responseToken->length = mech_token->length;
	nt.u.negTokenResp.responseToken->data   = mech_token->value;
	mech_token->length = 0;
	mech_token->value  = NULL;

	*(nt.u.negTokenResp.negResult)  = accept_incomplete;
    }

    if (mech_buf != GSS_C_NO_BUFFER) {

	ret = gss_get_mic(minor_status,
			  context_handle->negotiated_ctx_id,
			  0,
			  mech_buf,
			  &mic_buf);
	if (ret == GSS_S_COMPLETE) {
	    ALLOC(nt.u.negTokenResp.mechListMIC, 1);
	    if (nt.u.negTokenResp.mechListMIC == NULL) {
		gss_release_buffer(minor_status, &mic_buf);
		free_NegotiationToken(&nt);
		*minor_status = ENOMEM;
		return GSS_S_FAILURE;
	    }

	    nt.u.negTokenResp.mechListMIC->length = mic_buf.length;
	    nt.u.negTokenResp.mechListMIC->data   = mic_buf.value;
	} else if (ret == GSS_S_UNAVAILABLE) {
	    nt.u.negTokenResp.mechListMIC = NULL;
	} if (ret) {
	    free_NegotiationToken(&nt);
	    *minor_status = ENOMEM;
	    return GSS_S_FAILURE;
	}
    } else {
	nt.u.negTokenResp.mechListMIC = NULL;
    }

    ASN1_MALLOC_ENCODE(NegotiationToken,
		       output_token->value, output_token->length,
		       &nt, &size, ret);
    if (ret) {
	free_NegotiationToken(&nt);
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    if (*(nt.u.negTokenResp.negResult) == accept_completed)
	ret = GSS_S_COMPLETE;
    else
	ret = GSS_S_CONTINUE_NEEDED;

    free_NegotiationToken(&nt);
    return ret;
}

static OM_uint32
spnego_initial
           (OM_uint32 * minor_status,
	    gss_cred_id_t cred,
            gss_ctx_id_t * context_handle,
            const gss_name_t target_name,
            const gss_OID mech_type,
            OM_uint32 req_flags,
            OM_uint32 time_req,
            const gss_channel_bindings_t input_chan_bindings,
            const gss_buffer_t input_token,
            gss_OID * actual_mech_type,
            gss_buffer_t output_token,
            OM_uint32 * ret_flags,
            OM_uint32 * time_rec
    )
{
    NegTokenInit ni;
    int ret;
    OM_uint32 sub, minor;
    gss_buffer_desc mech_token;
    u_char *buf;
    size_t buf_size, buf_len;
    gss_buffer_desc data;
    size_t ni_len;
    gss_ctx_id_t context;
    gssspnego_ctx ctx;
    spnego_name name = (spnego_name)target_name;

    *minor_status = 0;

    memset (&ni, 0, sizeof(ni));

    *context_handle = GSS_C_NO_CONTEXT;

    if (target_name == GSS_C_NO_NAME)
	return GSS_S_BAD_NAME;

    sub = _gss_spnego_alloc_sec_context(&minor, &context);
    if (GSS_ERROR(sub)) {
	*minor_status = minor;
	return sub;
    }
    ctx = (gssspnego_ctx)context;

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);

    ctx->local = 1;

    sub = gss_import_name(&minor, &name->value, &name->type, &ctx->target_name);
    if (GSS_ERROR(sub)) {
	*minor_status = minor;
	_gss_spnego_internal_delete_sec_context(&minor, &context, GSS_C_NO_BUFFER);
	return sub;
    }

    sub = _gss_spnego_indicate_mechtypelist(&minor,
					    ctx->target_name,
					    initiator_approved,
					    0,
					    cred,
					    &ni.mechTypes,
					    &ctx->preferred_mech_type);
    if (GSS_ERROR(sub)) {
	*minor_status = minor;
	_gss_spnego_internal_delete_sec_context(&minor, &context, GSS_C_NO_BUFFER);
	return sub;
    }

    ni.reqFlags = NULL;

    /*
     * If we have a credential handle, use it to select the mechanism
     * that we will use
     */

    /* generate optimistic token */
    sub = gss_init_sec_context(&minor,
			       cred,
			       &ctx->negotiated_ctx_id,
			       ctx->target_name,
			       ctx->preferred_mech_type,
			       req_flags,
			       time_req,
			       input_chan_bindings,
			       input_token,
			       &ctx->negotiated_mech_type,
			       &mech_token,
			       &ctx->mech_flags,
			       &ctx->mech_time_rec);
    if (GSS_ERROR(sub)) {
	free_NegTokenInit(&ni);
	*minor_status = minor;
	gss_mg_collect_error(ctx->preferred_mech_type, sub, minor);
	_gss_spnego_internal_delete_sec_context(&minor, &context, GSS_C_NO_BUFFER);
	return sub;
    }
    if (sub == GSS_S_COMPLETE)
	ctx->maybe_open = 1;

    if (mech_token.length != 0) {
	ALLOC(ni.mechToken, 1);
	if (ni.mechToken == NULL) {
	    free_NegTokenInit(&ni);
	    gss_release_buffer(&minor, &mech_token);
	    _gss_spnego_internal_delete_sec_context(&minor, &context, GSS_C_NO_BUFFER);
	    *minor_status = ENOMEM;
	    return GSS_S_FAILURE;
	}
	ni.mechToken->length = mech_token.length;
	ni.mechToken->data = malloc(mech_token.length);
	if (ni.mechToken->data == NULL && mech_token.length != 0) {
	    free_NegTokenInit(&ni);
	    gss_release_buffer(&minor, &mech_token);
	    *minor_status = ENOMEM;
	    _gss_spnego_internal_delete_sec_context(&minor, &context, GSS_C_NO_BUFFER);
	    return GSS_S_FAILURE;
	}
	memcpy(ni.mechToken->data, mech_token.value, mech_token.length);
	gss_release_buffer(&minor, &mech_token);
    } else
	ni.mechToken = NULL;

    ni.mechListMIC = NULL;

    ni_len = length_NegTokenInit(&ni);
    buf_size = 1 + der_length_len(ni_len) + ni_len;

    buf = malloc(buf_size);
    if (buf == NULL) {
	free_NegTokenInit(&ni);
	*minor_status = ENOMEM;
	_gss_spnego_internal_delete_sec_context(&minor, &context, GSS_C_NO_BUFFER);
	return GSS_S_FAILURE;
    }

    ret = encode_NegTokenInit(buf + buf_size - 1,
			      ni_len,
			      &ni, &buf_len);
    if (ret == 0 && ni_len != buf_len)
	abort();

    if (ret == 0) {
	size_t tmp;

	ret = der_put_length_and_tag(buf + buf_size - buf_len - 1,
				     buf_size - buf_len,
				     buf_len,
				     ASN1_C_CONTEXT,
				     CONS,
				     0,
				     &tmp);
	if (ret == 0 && tmp + buf_len != buf_size)
	    abort();
    }
    if (ret) {
	*minor_status = ret;
	free(buf);
	free_NegTokenInit(&ni);
	_gss_spnego_internal_delete_sec_context(&minor, &context, GSS_C_NO_BUFFER);
	return GSS_S_FAILURE;
    }

    data.value  = buf;
    data.length = buf_size;

    ctx->initiator_mech_types.len = ni.mechTypes.len;
    ctx->initiator_mech_types.val = ni.mechTypes.val;
    ni.mechTypes.len = 0;
    ni.mechTypes.val = NULL;

    free_NegTokenInit(&ni);

    sub = gss_encapsulate_token(&data,
				GSS_SPNEGO_MECHANISM,
				output_token);
    free (buf);

    if (sub) {
	_gss_spnego_internal_delete_sec_context(&minor, &context, GSS_C_NO_BUFFER);
	return sub;
    }

    if (actual_mech_type)
	*actual_mech_type = ctx->negotiated_mech_type;
    if (ret_flags)
	*ret_flags = ctx->mech_flags;
    if (time_rec)
	*time_rec = ctx->mech_time_rec;

    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);

    *context_handle = context;

    return GSS_S_CONTINUE_NEEDED;
}

static OM_uint32
spnego_reply
           (OM_uint32 * minor_status,
	    const gss_cred_id_t cred,
            gss_ctx_id_t * context_handle,
            const gss_name_t target_name,
            const gss_OID mech_type,
            OM_uint32 req_flags,
            OM_uint32 time_req,
            const gss_channel_bindings_t input_chan_bindings,
            const gss_buffer_t input_token,
            gss_OID * actual_mech_type,
            gss_buffer_t output_token,
            OM_uint32 * ret_flags,
            OM_uint32 * time_rec
    )
{
    OM_uint32 ret, minor;
    NegotiationToken resp;
    gss_OID_desc mech;
    int require_mic;
    size_t buf_len = 0;
    gss_buffer_desc mic_buf, mech_buf;
    gss_buffer_desc mech_output_token;
    gssspnego_ctx ctx;

    *minor_status = 0;

    ctx = (gssspnego_ctx)*context_handle;

    output_token->length = 0;
    output_token->value  = NULL;

    mech_output_token.length = 0;
    mech_output_token.value = NULL;

    mech_buf.value = NULL;
    mech_buf.length = 0;

    ret = decode_NegotiationToken(input_token->value, input_token->length,
				  &resp, NULL);
    if (ret)
      return ret;

    if (resp.element != choice_NegotiationToken_negTokenResp) {
	free_NegotiationToken(&resp);
	*minor_status = 0;
	return GSS_S_BAD_MECH;
    }

    if (resp.u.negTokenResp.negResult == NULL
	|| *(resp.u.negTokenResp.negResult) == reject
	/* || resp.u.negTokenResp.supportedMech == NULL */
	)
    {
	free_NegotiationToken(&resp);
	return GSS_S_BAD_MECH;
    }

    /*
     * Pick up the mechanism that the acceptor selected, only allow it
     * to be sent in packet.
     */

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);

    if (resp.u.negTokenResp.supportedMech) {

	if (ctx->oidlen) {
	    free_NegotiationToken(&resp);
	    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	    return GSS_S_BAD_MECH;
	}
	ret = der_put_oid(ctx->oidbuf + sizeof(ctx->oidbuf) - 1,
			  sizeof(ctx->oidbuf),
			  resp.u.negTokenResp.supportedMech,
			  &ctx->oidlen);
	/* Avoid recursively embedded SPNEGO */
	if (ret || (ctx->oidlen == GSS_SPNEGO_MECHANISM->length &&
		    memcmp(ctx->oidbuf + sizeof(ctx->oidbuf) - ctx->oidlen,
			   GSS_SPNEGO_MECHANISM->elements,
			   ctx->oidlen) == 0))
	{
	    free_NegotiationToken(&resp);
	    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	    return GSS_S_BAD_MECH;
	}

	/* check if the acceptor took our optimistic token */
	if (ctx->oidlen != ctx->preferred_mech_type->length ||
	    memcmp(ctx->oidbuf + sizeof(ctx->oidbuf) - ctx->oidlen,
		   ctx->preferred_mech_type->elements,
		   ctx->oidlen) != 0)
	{
	    gss_delete_sec_context(&minor, &ctx->negotiated_ctx_id,
				   GSS_C_NO_BUFFER);
	    ctx->negotiated_ctx_id = GSS_C_NO_CONTEXT;
	}
    } else if (ctx->oidlen == 0) {
	free_NegotiationToken(&resp);
	HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	return GSS_S_BAD_MECH;
    }

    /* if a token (of non zero length), or no context, pass to underlaying mech */
    if ((resp.u.negTokenResp.responseToken != NULL && resp.u.negTokenResp.responseToken->length) ||
	ctx->negotiated_ctx_id == GSS_C_NO_CONTEXT) {
	gss_buffer_desc mech_input_token;

	if (resp.u.negTokenResp.responseToken) {
	    mech_input_token.length = resp.u.negTokenResp.responseToken->length;
	    mech_input_token.value  = resp.u.negTokenResp.responseToken->data;
	} else {
	    mech_input_token.length = 0;
	    mech_input_token.value = NULL;
	}


	mech.length = ctx->oidlen;
	mech.elements = ctx->oidbuf + sizeof(ctx->oidbuf) - ctx->oidlen;

	/* Fall through as if the negotiated mechanism
	   was requested explicitly */
	ret = gss_init_sec_context(&minor,
				   cred,
				   &ctx->negotiated_ctx_id,
				   ctx->target_name,
				   &mech,
				   req_flags,
				   time_req,
				   input_chan_bindings,
				   &mech_input_token,
				   &ctx->negotiated_mech_type,
				   &mech_output_token,
				   &ctx->mech_flags,
				   &ctx->mech_time_rec);
	if (GSS_ERROR(ret)) {
	    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	    free_NegotiationToken(&resp);
	    gss_mg_collect_error(&mech, ret, minor);
	    *minor_status = minor;
	    return ret;
	}
	if (ret == GSS_S_COMPLETE) {
	    ctx->open = 1;
	}
    } else if (*(resp.u.negTokenResp.negResult) == accept_completed) {
	if (ctx->maybe_open)
	    ctx->open = 1;
    }

    if (*(resp.u.negTokenResp.negResult) == request_mic) {
	ctx->require_mic = 1;
    }

    if (ctx->open) {
	/*
	 * Verify the mechListMIC if one was provided or CFX was
	 * used and a non-preferred mechanism was selected
	 */
	if (resp.u.negTokenResp.mechListMIC != NULL) {
	    require_mic = 1;
	} else {
	    ret = _gss_spnego_require_mechlist_mic(minor_status, ctx,
						   &require_mic);
	    if (ret) {
		HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
		free_NegotiationToken(&resp);
		gss_release_buffer(&minor, &mech_output_token);
		return ret;
	    }
	}
    } else {
	require_mic = 0;
    }

    if (require_mic) {
	ASN1_MALLOC_ENCODE(MechTypeList, mech_buf.value, mech_buf.length,
			   &ctx->initiator_mech_types, &buf_len, ret);
	if (ret) {
	    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	    free_NegotiationToken(&resp);
	    gss_release_buffer(&minor, &mech_output_token);
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}
	if (mech_buf.length != buf_len) {
	    abort();
            UNREACHABLE(return GSS_S_FAILURE);
        }

	if (resp.u.negTokenResp.mechListMIC == NULL) {
	    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	    free(mech_buf.value);
	    free_NegotiationToken(&resp);
	    *minor_status = 0;
	    return GSS_S_DEFECTIVE_TOKEN;
	}
	mic_buf.length = resp.u.negTokenResp.mechListMIC->length;
	mic_buf.value  = resp.u.negTokenResp.mechListMIC->data;

	if (mech_output_token.length == 0) {
	    ret = gss_verify_mic(minor_status,
				 ctx->negotiated_ctx_id,
				 &mech_buf,
				 &mic_buf,
				 NULL);
	   if (ret) {
		HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
		free(mech_buf.value);
		gss_release_buffer(&minor, &mech_output_token);
		free_NegotiationToken(&resp);
		return GSS_S_DEFECTIVE_TOKEN;
	    }
	    ctx->verified_mic = 1;
	}
    }

    ret = spnego_reply_internal(minor_status, ctx,
				require_mic ? &mech_buf : NULL,
				&mech_output_token,
				output_token);

    if (mech_buf.value != NULL)
	free(mech_buf.value);

    free_NegotiationToken(&resp);
    gss_release_buffer(&minor, &mech_output_token);

    if (actual_mech_type)
	*actual_mech_type = ctx->negotiated_mech_type;
    if (ret_flags)
	*ret_flags = ctx->mech_flags;
    if (time_rec)
	*time_rec = ctx->mech_time_rec;

    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
    return ret;
}

OM_uint32 GSSAPI_CALLCONV
_gss_spnego_init_sec_context
           (OM_uint32 * minor_status,
            const gss_cred_id_t initiator_cred_handle,
            gss_ctx_id_t * context_handle,
            const gss_name_t target_name,
            const gss_OID mech_type,
            OM_uint32 req_flags,
            OM_uint32 time_req,
            const gss_channel_bindings_t input_chan_bindings,
            const gss_buffer_t input_token,
            gss_OID * actual_mech_type,
            gss_buffer_t output_token,
            OM_uint32 * ret_flags,
            OM_uint32 * time_rec
           )
{
    if (*context_handle == GSS_C_NO_CONTEXT)
	return spnego_initial (minor_status,
			       initiator_cred_handle,
			       context_handle,
			       target_name,
			       mech_type,
			       req_flags,
			       time_req,
			       input_chan_bindings,
			       input_token,
			       actual_mech_type,
			       output_token,
			       ret_flags,
			       time_rec);
    else
	return spnego_reply (minor_status,
			     initiator_cred_handle,
			     context_handle,
			     target_name,
			     mech_type,
			     req_flags,
			     time_req,
			     input_chan_bindings,
			     input_token,
			     actual_mech_type,
			     output_token,
			     ret_flags,
			     time_rec);
}

