/*
 * Copyright (c) 2008  Kungliga Tekniska Högskolan
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

#include <roken.h>

#if 0
OM_uint32 GSSAPI_CALLCONV
_gk_wrap_iov(OM_uint32 * minor_status,
	     gss_ctx_id_t  context_handle,
	     int conf_req_flag,
	     gss_qop_t qop_req,
	     int * conf_state,
	     gss_iov_buffer_desc *iov,
	     int iov_count)
{
  const gsskrb5_ctx ctx = (const gsskrb5_ctx) context_handle;
  krb5_context context;

  GSSAPI_KRB5_INIT (&context);

  if (ctx->more_flags & IS_CFX)
      return _gssapi_wrap_cfx_iov(minor_status, ctx, context,
				  conf_req_flag, conf_state,
				  iov, iov_count);

    return GSS_S_FAILURE;
}

OM_uint32 GSSAPI_CALLCONV
_gk_unwrap_iov(OM_uint32 *minor_status,
	       gss_ctx_id_t context_handle,
	       int *conf_state,
	       gss_qop_t *qop_state,
	       gss_iov_buffer_desc *iov,
	       int iov_count)
{
    const gsskrb5_ctx ctx = (const gsskrb5_ctx) context_handle;
    krb5_context context;

    GSSAPI_KRB5_INIT (&context);

    if (ctx->more_flags & IS_CFX)
	return _gssapi_unwrap_cfx_iov(minor_status, ctx, context,
				      conf_state, qop_state, iov, iov_count);

    return GSS_S_FAILURE;
}
#endif

OM_uint32 GSSAPI_CALLCONV
_gk_wrap_iov_length(OM_uint32 * minor_status,
		    gss_ctx_id_t context_handle,
		    int conf_req_flag,
		    gss_qop_t qop_req,
		    int *conf_state,
		    gss_iov_buffer_desc *iov,
		    int iov_count)
{
    const gsskrb5_ctx ctx = (const gsskrb5_ctx) context_handle;
    krb5_context context;

    GSSAPI_KRB5_INIT (&context);

    if (ctx->more_flags & IS_CFX)
	return _gssapi_wrap_iov_length_cfx(minor_status, ctx, context,
					   conf_req_flag, qop_req, conf_state,
					   iov, iov_count);

    return GSS_S_FAILURE;
}
