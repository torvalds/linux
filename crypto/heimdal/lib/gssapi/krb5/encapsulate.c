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

void
_gssapi_encap_length (size_t data_len,
		      size_t *len,
		      size_t *total_len,
		      const gss_OID mech)
{
    size_t len_len;

    *len = 1 + 1 + mech->length + data_len;

    len_len = der_length_len(*len);

    *total_len = 1 + len_len + *len;
}

void
_gsskrb5_encap_length (size_t data_len,
			  size_t *len,
			  size_t *total_len,
			  const gss_OID mech)
{
    _gssapi_encap_length(data_len + 2, len, total_len, mech);
}

void *
_gsskrb5_make_header (void *ptr,
			 size_t len,
			 const void *type,
			 const gss_OID mech)
{
    u_char *p = ptr;
    p = _gssapi_make_mech_header(p, len, mech);
    memcpy (p, type, 2);
    p += 2;
    return p;
}

void *
_gssapi_make_mech_header(void *ptr,
			 size_t len,
			 const gss_OID mech)
{
    u_char *p = ptr;
    int e;
    size_t len_len, foo;

    *p++ = 0x60;
    len_len = der_length_len(len);
    e = der_put_length (p + len_len - 1, len_len, len, &foo);
    if(e || foo != len_len)
	abort ();
    p += len_len;
    *p++ = 0x06;
    *p++ = mech->length;
    memcpy (p, mech->elements, mech->length);
    p += mech->length;
    return p;
}

/*
 * Give it a krb5_data and it will encapsulate with extra GSS-API wrappings.
 */

OM_uint32
_gssapi_encapsulate(
    OM_uint32 *minor_status,
    const krb5_data *in_data,
    gss_buffer_t output_token,
    const gss_OID mech
)
{
    size_t len, outer_len;
    void *p;

    _gssapi_encap_length (in_data->length, &len, &outer_len, mech);

    output_token->length = outer_len;
    output_token->value  = malloc (outer_len);
    if (output_token->value == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    p = _gssapi_make_mech_header (output_token->value, len, mech);
    memcpy (p, in_data->data, in_data->length);
    return GSS_S_COMPLETE;
}

/*
 * Give it a krb5_data and it will encapsulate with extra GSS-API krb5
 * wrappings.
 */

OM_uint32
_gsskrb5_encapsulate(
			OM_uint32 *minor_status,
			const krb5_data *in_data,
			gss_buffer_t output_token,
			const void *type,
			const gss_OID mech
)
{
    size_t len, outer_len;
    u_char *p;

    _gsskrb5_encap_length (in_data->length, &len, &outer_len, mech);

    output_token->length = outer_len;
    output_token->value  = malloc (outer_len);
    if (output_token->value == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    p = _gsskrb5_make_header (output_token->value, len, type, mech);
    memcpy (p, in_data->data, in_data->length);
    return GSS_S_COMPLETE;
}
