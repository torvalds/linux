/*
 * Copyright (c) 1997 - 2001 Kungliga Tekniska Högskolan
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

/*
 * return the length of the mechanism in token or -1
 * (which implies that the token was bad - GSS_S_DEFECTIVE_TOKEN
 */

ssize_t
_gsskrb5_get_mech (const u_char *ptr,
		      size_t total_len,
		      const u_char **mech_ret)
{
    size_t len, len_len, mech_len, foo;
    const u_char *p = ptr;
    int e;

    if (total_len < 1)
	return -1;
    if (*p++ != 0x60)
	return -1;
    e = der_get_length (p, total_len - 1, &len, &len_len);
    if (e || 1 + len_len + len != total_len)
	return -1;
    p += len_len;
    if (*p++ != 0x06)
	return -1;
    e = der_get_length (p, total_len - 1 - len_len - 1,
			&mech_len, &foo);
    if (e)
	return -1;
    p += foo;
    *mech_ret = p;
    return mech_len;
}

OM_uint32
_gssapi_verify_mech_header(u_char **str,
			   size_t total_len,
			   gss_OID mech)
{
    const u_char *p;
    ssize_t mech_len;

    mech_len = _gsskrb5_get_mech (*str, total_len, &p);
    if (mech_len < 0)
	return GSS_S_DEFECTIVE_TOKEN;

    if (mech_len != mech->length)
	return GSS_S_BAD_MECH;
    if (ct_memcmp(p,
		  mech->elements,
		  mech->length) != 0)
	return GSS_S_BAD_MECH;
    p += mech_len;
    *str = rk_UNCONST(p);
    return GSS_S_COMPLETE;
}

OM_uint32
_gsskrb5_verify_header(u_char **str,
			  size_t total_len,
			  const void *type,
			  gss_OID oid)
{
    OM_uint32 ret;
    size_t len;
    u_char *p = *str;

    ret = _gssapi_verify_mech_header(str, total_len, oid);
    if (ret)
	return ret;

    len = total_len - (*str - p);

    if (len < 2)
	return GSS_S_DEFECTIVE_TOKEN;

    if (ct_memcmp (*str, type, 2) != 0)
	return GSS_S_DEFECTIVE_TOKEN;
    *str += 2;

    return 0;
}

/*
 * Remove the GSS-API wrapping from `in_token' giving `out_data.
 * Does not copy data, so just free `in_token'.
 */

OM_uint32
_gssapi_decapsulate(
    OM_uint32 *minor_status,
    gss_buffer_t input_token_buffer,
    krb5_data *out_data,
    const gss_OID mech
)
{
    u_char *p;
    OM_uint32 ret;

    p = input_token_buffer->value;
    ret = _gssapi_verify_mech_header(&p,
				    input_token_buffer->length,
				    mech);
    if (ret) {
	*minor_status = 0;
	return ret;
    }

    out_data->length = input_token_buffer->length -
	(p - (u_char *)input_token_buffer->value);
    out_data->data   = p;
    return GSS_S_COMPLETE;
}

/*
 * Remove the GSS-API wrapping from `in_token' giving `out_data.
 * Does not copy data, so just free `in_token'.
 */

OM_uint32
_gsskrb5_decapsulate(OM_uint32 *minor_status,
			gss_buffer_t input_token_buffer,
			krb5_data *out_data,
			const void *type,
			gss_OID oid)
{
    u_char *p;
    OM_uint32 ret;

    p = input_token_buffer->value;
    ret = _gsskrb5_verify_header(&p,
				    input_token_buffer->length,
				    type,
				    oid);
    if (ret) {
	*minor_status = 0;
	return ret;
    }

    out_data->length = input_token_buffer->length -
	(p - (u_char *)input_token_buffer->value);
    out_data->data   = p;
    return GSS_S_COMPLETE;
}

/*
 * Verify padding of a gss wrapped message and return its length.
 */

OM_uint32
_gssapi_verify_pad(gss_buffer_t wrapped_token,
		   size_t datalen,
		   size_t *padlen)
{
    u_char *pad;
    size_t padlength;
    int i;

    pad = (u_char *)wrapped_token->value + wrapped_token->length - 1;
    padlength = *pad;

    if (padlength > datalen)
	return GSS_S_BAD_MECH;

    for (i = padlength; i > 0 && *pad == padlength; i--, pad--)
	;
    if (i != 0)
	return GSS_S_BAD_MIC;

    *padlen = padlength;

    return 0;
}
