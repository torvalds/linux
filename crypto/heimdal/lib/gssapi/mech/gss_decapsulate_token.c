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

#include "mech_locl.h"

GSSAPI_LIB_FUNCTION OM_uint32 GSSAPI_LIB_CALL
gss_decapsulate_token(gss_const_buffer_t input_token,
		      gss_const_OID oid,
		      gss_buffer_t output_token)
{
    GSSAPIContextToken ct;
    heim_oid o;
    OM_uint32 status;
    int ret;
    size_t size;

    _mg_buffer_zero(output_token);

    ret = der_get_oid (oid->elements, oid->length, &o, &size);
    if (ret)
	return GSS_S_FAILURE;

    ret = decode_GSSAPIContextToken(input_token->value, input_token->length,
				    &ct, NULL);
    if (ret) {
	der_free_oid(&o);
	return GSS_S_FAILURE;
    }

    if (der_heim_oid_cmp(&ct.thisMech, &o) == 0) {
	status = GSS_S_COMPLETE;
	output_token->value = ct.innerContextToken.data;
	output_token->length = ct.innerContextToken.length;
	der_free_oid(&ct.thisMech);
    } else {
	free_GSSAPIContextToken(&ct);
 	status = GSS_S_FAILURE;
    }
    der_free_oid(&o);

    return status;
}
