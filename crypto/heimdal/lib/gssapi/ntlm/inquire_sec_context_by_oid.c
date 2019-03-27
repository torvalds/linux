/*
 * Copyright (c) 2006 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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

OM_uint32 GSSAPI_CALLCONV
_gss_ntlm_inquire_sec_context_by_oid(OM_uint32 *minor_status,
				     const gss_ctx_id_t context_handle,
				     const gss_OID desired_object,
				     gss_buffer_set_t *data_set)
{
    ntlm_ctx ctx = (ntlm_ctx)context_handle;

    if (ctx == NULL) {
	*minor_status = 0;
	return GSS_S_NO_CONTEXT;
    }

    if (gss_oid_equal(desired_object, GSS_NTLM_GET_SESSION_KEY_X) ||
        gss_oid_equal(desired_object, GSS_C_INQ_SSPI_SESSION_KEY)) {
	gss_buffer_desc value;

	value.length = ctx->sessionkey.length;
	value.value = ctx->sessionkey.data;

	return gss_add_buffer_set_member(minor_status,
					 &value,
					 data_set);
    } else if (gss_oid_equal(desired_object, GSS_C_INQ_WIN2K_PAC_X)) {
	if (ctx->pac.length == 0) {
	    *minor_status = ENOENT;
	    return GSS_S_FAILURE;
	}

	return gss_add_buffer_set_member(minor_status,
					 &ctx->pac,
					 data_set);

    } else if (gss_oid_equal(desired_object, GSS_C_NTLM_AVGUEST)) {
	gss_buffer_desc value;
	uint32_t num;

	if (ctx->kcmflags & KCM_NTLM_FLAG_AV_GUEST)
	    num = 1;
	else
	    num = 0;

	value.length = sizeof(num);
	value.value = &num;

	return gss_add_buffer_set_member(minor_status,
					 &value,
					 data_set);
    } else {
	*minor_status = 0;
	return GSS_S_FAILURE;
    }
}
