/*-
 * Copyright (c) 2005 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD: src/lib/libgssapi/gss_wrap.c,v 1.1 2005/12/29 14:40:20 dfr Exp $
 */

#include "mech_locl.h"

/**
 * Wrap a message using either confidentiality (encryption +
 * signature) or sealing (signature).
 *
 * @param minor_status minor status code.
 * @param context_handle context handle.
 * @param conf_req_flag if non zero, confidentiality is requestd.
 * @param qop_req type of protection needed, in most cases it GSS_C_QOP_DEFAULT should be passed in.
 * @param input_message_buffer messages to wrap
 * @param conf_state returns non zero if confidentiality was honoured.
 * @param output_message_buffer the resulting buffer, release with gss_release_buffer().
 *
 * @ingroup gssapi
 */

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_wrap(OM_uint32 *minor_status,
    const gss_ctx_id_t context_handle,
    int conf_req_flag,
    gss_qop_t qop_req,
    const gss_buffer_t input_message_buffer,
    int *conf_state,
    gss_buffer_t output_message_buffer)
{
	struct _gss_context *ctx = (struct _gss_context *) context_handle;
	gssapi_mech_interface m;

	if (conf_state)
	    *conf_state = 0;
	_mg_buffer_zero(output_message_buffer);
	if (ctx == NULL) {
	    *minor_status = 0;
	    return GSS_S_NO_CONTEXT;
	}

	m = ctx->gc_mech;

	return (m->gm_wrap(minor_status, ctx->gc_ctx,
		    conf_req_flag, qop_req, input_message_buffer,
		    conf_state, output_message_buffer));
}
