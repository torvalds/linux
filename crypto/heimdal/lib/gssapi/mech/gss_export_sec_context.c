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
 *	$FreeBSD: src/lib/libgssapi/gss_export_sec_context.c,v 1.1 2005/12/29 14:40:20 dfr Exp $
 */

#include "mech_locl.h"

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_export_sec_context(OM_uint32 *minor_status,
    gss_ctx_id_t *context_handle,
    gss_buffer_t interprocess_token)
{
	OM_uint32 major_status;
	struct _gss_context *ctx = (struct _gss_context *) *context_handle;
	gssapi_mech_interface m = ctx->gc_mech;
	gss_buffer_desc buf;

	_mg_buffer_zero(interprocess_token);

	major_status = m->gm_export_sec_context(minor_status,
	    &ctx->gc_ctx, &buf);

	if (major_status == GSS_S_COMPLETE) {
		unsigned char *p;

		free(ctx);
		*context_handle = GSS_C_NO_CONTEXT;
		interprocess_token->length = buf.length
			+ 2 + m->gm_mech_oid.length;
		interprocess_token->value = malloc(interprocess_token->length);
		if (!interprocess_token->value) {
			/*
			 * We are in trouble here - the context is
			 * already gone. This is allowed as long as we
			 * set the caller's context_handle to
			 * GSS_C_NO_CONTEXT, which we did above.
			 * Return GSS_S_FAILURE.
			 */
			_mg_buffer_zero(interprocess_token);
			*minor_status = ENOMEM;
			return (GSS_S_FAILURE);
		}
		p = interprocess_token->value;
		p[0] = m->gm_mech_oid.length >> 8;
		p[1] = m->gm_mech_oid.length;
		memcpy(p + 2, m->gm_mech_oid.elements, m->gm_mech_oid.length);
		memcpy(p + 2 + m->gm_mech_oid.length, buf.value, buf.length);
		gss_release_buffer(minor_status, &buf);
	} else {
		_gss_mg_error(m, major_status, *minor_status);
	}

	return (major_status);
}
