/*
 * Copyright (c) 2007 Kungliga Tekniska Högskolan
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
_gsskrb5_pseudo_random(OM_uint32 *minor_status,
		       gss_ctx_id_t context_handle,
		       int prf_key,
		       const gss_buffer_t prf_in,
		       ssize_t desired_output_len,
		       gss_buffer_t prf_out)
{
    gsskrb5_ctx ctx = (gsskrb5_ctx)context_handle;
    krb5_context context;
    krb5_error_code ret;
    krb5_crypto crypto;
    krb5_data input, output;
    uint32_t num;
    OM_uint32 junk;
    unsigned char *p;
    krb5_keyblock *key = NULL;
    size_t dol;

    if (ctx == NULL) {
	*minor_status = 0;
	return GSS_S_NO_CONTEXT;
    }

    if (desired_output_len <= 0 || prf_in->length + 4 < prf_in->length) {
	*minor_status = 0;
	return GSS_S_FAILURE;
    }
    dol = desired_output_len;

    GSSAPI_KRB5_INIT (&context);

    switch(prf_key) {
    case GSS_C_PRF_KEY_FULL:
	_gsskrb5i_get_acceptor_subkey(ctx, context, &key);
	break;
    case GSS_C_PRF_KEY_PARTIAL:
	_gsskrb5i_get_initiator_subkey(ctx, context, &key);
	break;
    default:
	_gsskrb5_set_status(EINVAL, "unknown kerberos prf_key");
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    if (key == NULL) {
	_gsskrb5_set_status(EINVAL, "no prf_key found");
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    ret = krb5_crypto_init(context, key, 0, &crypto);
    krb5_free_keyblock (context, key);
    if (ret) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    prf_out->value = malloc(dol);
    if (prf_out->value == NULL) {
	_gsskrb5_set_status(GSS_KRB5_S_KG_INPUT_TOO_LONG, "Out of memory");
	*minor_status = GSS_KRB5_S_KG_INPUT_TOO_LONG;
	krb5_crypto_destroy(context, crypto);
	return GSS_S_FAILURE;
    }
    prf_out->length = dol;

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);

    input.length = prf_in->length + 4;
    input.data = malloc(prf_in->length + 4);
    if (input.data == NULL) {
	_gsskrb5_set_status(GSS_KRB5_S_KG_INPUT_TOO_LONG, "Out of memory");
	*minor_status = GSS_KRB5_S_KG_INPUT_TOO_LONG;
	gss_release_buffer(&junk, prf_out);
	krb5_crypto_destroy(context, crypto);
	HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	return GSS_S_FAILURE;
    }
    memcpy(((uint8_t *)input.data) + 4, prf_in->value, prf_in->length);

    num = 0;
    p = prf_out->value;
    while(dol > 0) {
	size_t tsize;

	_gsskrb5_encode_be_om_uint32(num, input.data);

	ret = krb5_crypto_prf(context, crypto, &input, &output);
	if (ret) {
	    *minor_status = ret;
	    free(input.data);
	    gss_release_buffer(&junk, prf_out);
	    krb5_crypto_destroy(context, crypto);
	    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	    return GSS_S_FAILURE;
	}

	tsize = min(dol, output.length);
	memcpy(p, output.data, tsize);
	p += tsize;
	dol -= tsize;
	krb5_data_free(&output);
	num++;
    }
    free(input.data);

    krb5_crypto_destroy(context, crypto);

    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);

    return GSS_S_COMPLETE;
}
