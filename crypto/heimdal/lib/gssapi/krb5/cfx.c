/*
 * Copyright (c) 2003, PADL Software Pty Ltd.
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
 * 3. Neither the name of PADL Software nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PADL SOFTWARE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL PADL SOFTWARE OR CONTRIBUTORS BE LIABLE
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
 * Implementation of RFC 4121
 */

#define CFXSentByAcceptor	(1 << 0)
#define CFXSealed		(1 << 1)
#define CFXAcceptorSubkey	(1 << 2)

krb5_error_code
_gsskrb5cfx_wrap_length_cfx(krb5_context context,
			    krb5_crypto crypto,
			    int conf_req_flag,
			    int dce_style,
			    size_t input_length,
			    size_t *output_length,
			    size_t *cksumsize,
			    uint16_t *padlength)
{
    krb5_error_code ret;
    krb5_cksumtype type;

    /* 16-byte header is always first */
    *output_length = sizeof(gss_cfx_wrap_token_desc);
    *padlength = 0;

    ret = krb5_crypto_get_checksum_type(context, crypto, &type);
    if (ret)
	return ret;

    ret = krb5_checksumsize(context, type, cksumsize);
    if (ret)
	return ret;

    if (conf_req_flag) {
	size_t padsize;

	/* Header is concatenated with data before encryption */
	input_length += sizeof(gss_cfx_wrap_token_desc);

	if (dce_style) {
		ret = krb5_crypto_getblocksize(context, crypto, &padsize);
	} else {
		ret = krb5_crypto_getpadsize(context, crypto, &padsize);
	}
	if (ret) {
	    return ret;
	}
	if (padsize > 1) {
	    /* XXX check this */
	    *padlength = padsize - (input_length % padsize);

	    /* We add the pad ourselves (noted here for completeness only) */
	    input_length += *padlength;
	}

	*output_length += krb5_get_wrapped_length(context,
						  crypto, input_length);
    } else {
	/* Checksum is concatenated with data */
	*output_length += input_length + *cksumsize;
    }

    assert(*output_length > input_length);

    return 0;
}

OM_uint32
_gssapi_wrap_size_cfx(OM_uint32 *minor_status,
		      const gsskrb5_ctx ctx,
		      krb5_context context,
		      int conf_req_flag,
		      gss_qop_t qop_req,
		      OM_uint32 req_output_size,
		      OM_uint32 *max_input_size)
{
    krb5_error_code ret;

    *max_input_size = 0;

    /* 16-byte header is always first */
    if (req_output_size < 16)
	return 0;
    req_output_size -= 16;

    if (conf_req_flag) {
	size_t wrapped_size, sz;

	wrapped_size = req_output_size + 1;
	do {
	    wrapped_size--;
	    sz = krb5_get_wrapped_length(context,
					 ctx->crypto, wrapped_size);
	} while (wrapped_size && sz > req_output_size);
	if (wrapped_size == 0)
	    return 0;

	/* inner header */
	if (wrapped_size < 16)
	    return 0;

	wrapped_size -= 16;

	*max_input_size = wrapped_size;
    } else {
	krb5_cksumtype type;
	size_t cksumsize;

	ret = krb5_crypto_get_checksum_type(context, ctx->crypto, &type);
	if (ret)
	    return ret;

	ret = krb5_checksumsize(context, type, &cksumsize);
	if (ret)
	    return ret;

	if (req_output_size < cksumsize)
	    return 0;

	/* Checksum is concatenated with data */
	*max_input_size = req_output_size - cksumsize;
    }

    return 0;
}

/*
 * Rotate "rrc" bytes to the front or back
 */

static krb5_error_code
rrc_rotate(void *data, size_t len, uint16_t rrc, krb5_boolean unrotate)
{
    u_char *tmp, buf[256];
    size_t left;

    if (len == 0)
	return 0;

    rrc %= len;

    if (rrc == 0)
	return 0;

    left = len - rrc;

    if (rrc <= sizeof(buf)) {
	tmp = buf;
    } else {
	tmp = malloc(rrc);
	if (tmp == NULL)
	    return ENOMEM;
    }

    if (unrotate) {
	memcpy(tmp, data, rrc);
	memmove(data, (u_char *)data + rrc, left);
	memcpy((u_char *)data + left, tmp, rrc);
    } else {
	memcpy(tmp, (u_char *)data + left, rrc);
	memmove((u_char *)data + rrc, data, left);
	memcpy(data, tmp, rrc);
    }

    if (rrc > sizeof(buf))
	free(tmp);

    return 0;
}

gss_iov_buffer_desc *
_gk_find_buffer(gss_iov_buffer_desc *iov, int iov_count, OM_uint32 type)
{
    int i;

    for (i = 0; i < iov_count; i++)
	if (type == GSS_IOV_BUFFER_TYPE(iov[i].type))
	    return &iov[i];
    return NULL;
}

OM_uint32
_gk_allocate_buffer(OM_uint32 *minor_status, gss_iov_buffer_desc *buffer, size_t size)
{
    if (buffer->type & GSS_IOV_BUFFER_FLAG_ALLOCATED) {
	if (buffer->buffer.length == size)
	    return GSS_S_COMPLETE;
	free(buffer->buffer.value);
    }

    buffer->buffer.value = malloc(size);
    buffer->buffer.length = size;
    if (buffer->buffer.value == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }
    buffer->type |= GSS_IOV_BUFFER_FLAG_ALLOCATED;

    return GSS_S_COMPLETE;
}


OM_uint32
_gk_verify_buffers(OM_uint32 *minor_status,
		   const gsskrb5_ctx ctx,
		   const gss_iov_buffer_desc *header,
		   const gss_iov_buffer_desc *padding,
		   const gss_iov_buffer_desc *trailer)
{
    if (header == NULL) {
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    if (IS_DCE_STYLE(ctx)) {
	/*
	 * In DCE style mode we reject having a padding or trailer buffer
	 */
	if (padding) {
	    *minor_status = EINVAL;
	    return GSS_S_FAILURE;
	}
	if (trailer) {
	    *minor_status = EINVAL;
	    return GSS_S_FAILURE;
	}
    } else {
	/*
	 * In non-DCE style mode we require having a padding buffer
	 */
	if (padding == NULL) {
	    *minor_status = EINVAL;
	    return GSS_S_FAILURE;
	}
    }

    *minor_status = 0;
    return GSS_S_COMPLETE;
}

#if 0
OM_uint32
_gssapi_wrap_cfx_iov(OM_uint32 *minor_status,
		     gsskrb5_ctx ctx,
		     krb5_context context,
		     int conf_req_flag,
		     int *conf_state,
		     gss_iov_buffer_desc *iov,
		     int iov_count)
{
    OM_uint32 major_status, junk;
    gss_iov_buffer_desc *header, *trailer, *padding;
    size_t gsshsize, k5hsize;
    size_t gsstsize, k5tsize;
    size_t rrc = 0, ec = 0;
    int i;
    gss_cfx_wrap_token token;
    krb5_error_code ret;
    int32_t seq_number;
    unsigned usage;
    krb5_crypto_iov *data = NULL;

    header = _gk_find_buffer(iov, iov_count, GSS_IOV_BUFFER_TYPE_HEADER);
    if (header == NULL) {
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    padding = _gk_find_buffer(iov, iov_count, GSS_IOV_BUFFER_TYPE_PADDING);
    if (padding != NULL) {
	padding->buffer.length = 0;
    }

    trailer = _gk_find_buffer(iov, iov_count, GSS_IOV_BUFFER_TYPE_TRAILER);

    major_status = _gk_verify_buffers(minor_status, ctx, header, padding, trailer);
    if (major_status != GSS_S_COMPLETE) {
	    return major_status;
    }

    if (conf_req_flag) {
	size_t k5psize = 0;
	size_t k5pbase = 0;
	size_t k5bsize = 0;
	size_t size = 0;

	for (i = 0; i < iov_count; i++) {
	    switch (GSS_IOV_BUFFER_TYPE(iov[i].type)) {
	    case GSS_IOV_BUFFER_TYPE_DATA:
		size += iov[i].buffer.length;
		break;
	    default:
		break;
	    }
	}

	size += sizeof(gss_cfx_wrap_token_desc);

	*minor_status = krb5_crypto_length(context, ctx->crypto,
					   KRB5_CRYPTO_TYPE_HEADER,
					   &k5hsize);
	if (*minor_status)
	    return GSS_S_FAILURE;

	*minor_status = krb5_crypto_length(context, ctx->crypto,
					   KRB5_CRYPTO_TYPE_TRAILER,
					   &k5tsize);
	if (*minor_status)
	    return GSS_S_FAILURE;

	*minor_status = krb5_crypto_length(context, ctx->crypto,
					   KRB5_CRYPTO_TYPE_PADDING,
					   &k5pbase);
	if (*minor_status)
	    return GSS_S_FAILURE;

	if (k5pbase > 1) {
	    k5psize = k5pbase - (size % k5pbase);
	} else {
	    k5psize = 0;
	}

	if (k5psize == 0 && IS_DCE_STYLE(ctx)) {
	    *minor_status = krb5_crypto_getblocksize(context, ctx->crypto,
						     &k5bsize);
	    if (*minor_status)
		return GSS_S_FAILURE;
	    ec = k5bsize;
	} else {
	    ec = k5psize;
	}

	gsshsize = sizeof(gss_cfx_wrap_token_desc) + k5hsize;
	gsstsize = sizeof(gss_cfx_wrap_token_desc) + ec + k5tsize;
    } else {
	if (IS_DCE_STYLE(ctx)) {
	    *minor_status = EINVAL;
	    return GSS_S_FAILURE;
	}

	k5hsize = 0;
	*minor_status = krb5_crypto_length(context, ctx->crypto,
					   KRB5_CRYPTO_TYPE_CHECKSUM,
					   &k5tsize);
	if (*minor_status)
	    return GSS_S_FAILURE;

	gsshsize = sizeof(gss_cfx_wrap_token_desc);
	gsstsize = k5tsize;
    }

    /*
     *
     */

    if (trailer == NULL) {
	rrc = gsstsize;
	if (IS_DCE_STYLE(ctx))
	    rrc -= ec;
	gsshsize += gsstsize;
	gsstsize = 0;
    } else if (GSS_IOV_BUFFER_FLAGS(trailer->type) & GSS_IOV_BUFFER_FLAG_ALLOCATE) {
	major_status = _gk_allocate_buffer(minor_status, trailer, gsstsize);
	if (major_status)
	    goto failure;
    } else if (trailer->buffer.length < gsstsize) {
	*minor_status = KRB5_BAD_MSIZE;
	major_status = GSS_S_FAILURE;
	goto failure;
    } else
	trailer->buffer.length = gsstsize;

    /*
     *
     */

    if (GSS_IOV_BUFFER_FLAGS(header->type) & GSS_IOV_BUFFER_FLAG_ALLOCATE) {
	major_status = _gk_allocate_buffer(minor_status, header, gsshsize);
	if (major_status != GSS_S_COMPLETE)
	    goto failure;
    } else if (header->buffer.length < gsshsize) {
	*minor_status = KRB5_BAD_MSIZE;
	major_status = GSS_S_FAILURE;
	goto failure;
    } else
	header->buffer.length = gsshsize;

    token = (gss_cfx_wrap_token)header->buffer.value;

    token->TOK_ID[0] = 0x05;
    token->TOK_ID[1] = 0x04;
    token->Flags     = 0;
    token->Filler    = 0xFF;

    if ((ctx->more_flags & LOCAL) == 0)
	token->Flags |= CFXSentByAcceptor;

    if (ctx->more_flags & ACCEPTOR_SUBKEY)
	token->Flags |= CFXAcceptorSubkey;

    if (ctx->more_flags & LOCAL)
	usage = KRB5_KU_USAGE_INITIATOR_SEAL;
    else
	usage = KRB5_KU_USAGE_ACCEPTOR_SEAL;

    if (conf_req_flag) {
	/*
	 * In Wrap tokens with confidentiality, the EC field is
	 * used to encode the size (in bytes) of the random filler.
	 */
	token->Flags |= CFXSealed;
	token->EC[0] = (ec >> 8) & 0xFF;
	token->EC[1] = (ec >> 0) & 0xFF;

    } else {
	/*
	 * In Wrap tokens without confidentiality, the EC field is
	 * used to encode the size (in bytes) of the trailing
	 * checksum.
	 *
	 * This is not used in the checksum calcuation itself,
	 * because the checksum length could potentially vary
	 * depending on the data length.
	 */
	token->EC[0] = 0;
	token->EC[1] = 0;
    }

    /*
     * In Wrap tokens that provide for confidentiality, the RRC
     * field in the header contains the hex value 00 00 before
     * encryption.
     *
     * In Wrap tokens that do not provide for confidentiality,
     * both the EC and RRC fields in the appended checksum
     * contain the hex value 00 00 for the purpose of calculating
     * the checksum.
     */
    token->RRC[0] = 0;
    token->RRC[1] = 0;

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);
    krb5_auth_con_getlocalseqnumber(context,
				    ctx->auth_context,
				    &seq_number);
    _gsskrb5_encode_be_om_uint32(0,          &token->SND_SEQ[0]);
    _gsskrb5_encode_be_om_uint32(seq_number, &token->SND_SEQ[4]);
    krb5_auth_con_setlocalseqnumber(context,
				    ctx->auth_context,
				    ++seq_number);
    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);

    data = calloc(iov_count + 3, sizeof(data[0]));
    if (data == NULL) {
	*minor_status = ENOMEM;
	major_status = GSS_S_FAILURE;
	goto failure;
    }

    if (conf_req_flag) {
	/*
	  plain packet:

	  {"header" | encrypt(plaintext-data | ec-padding | E"header")}

	  Expanded, this is with with RRC = 0:

	  {"header" | krb5-header | plaintext-data | ec-padding | E"header" | krb5-trailer }

	  In DCE-RPC mode == no trailer: RRC = gss "trailer" == length(ec-padding | E"header" | krb5-trailer)

	  {"header" | ec-padding | E"header" | krb5-trailer | krb5-header | plaintext-data  }
	 */

	i = 0;
	data[i].flags = KRB5_CRYPTO_TYPE_HEADER;
	data[i].data.data = ((uint8_t *)header->buffer.value) + header->buffer.length - k5hsize;
	data[i].data.length = k5hsize;

	for (i = 1; i < iov_count + 1; i++) {
	    switch (GSS_IOV_BUFFER_TYPE(iov[i - 1].type)) {
	    case GSS_IOV_BUFFER_TYPE_DATA:
		data[i].flags = KRB5_CRYPTO_TYPE_DATA;
		break;
	    case GSS_IOV_BUFFER_TYPE_SIGN_ONLY:
		data[i].flags = KRB5_CRYPTO_TYPE_SIGN_ONLY;
		break;
	    default:
		data[i].flags = KRB5_CRYPTO_TYPE_EMPTY;
		break;
	    }
	    data[i].data.length = iov[i - 1].buffer.length;
	    data[i].data.data = iov[i - 1].buffer.value;
	}

	/*
	 * Any necessary padding is added here to ensure that the
	 * encrypted token header is always at the end of the
	 * ciphertext.
	 */

	/* encrypted CFX header in trailer (or after the header if in
	   DCE mode). Copy in header into E"header"
	*/
	data[i].flags = KRB5_CRYPTO_TYPE_DATA;
	if (trailer)
	    data[i].data.data = trailer->buffer.value;
	else
	    data[i].data.data = ((uint8_t *)header->buffer.value) + sizeof(*token);

	data[i].data.length = ec + sizeof(*token);
	memset(data[i].data.data, 0xFF, ec);
	memcpy(((uint8_t *)data[i].data.data) + ec, token, sizeof(*token));
	i++;

	/* Kerberos trailer comes after the gss trailer */
	data[i].flags = KRB5_CRYPTO_TYPE_TRAILER;
	data[i].data.data = ((uint8_t *)data[i-1].data.data) + ec + sizeof(*token);
	data[i].data.length = k5tsize;
	i++;

	ret = krb5_encrypt_iov_ivec(context, ctx->crypto, usage, data, i, NULL);
	if (ret != 0) {
	    *minor_status = ret;
	    major_status = GSS_S_FAILURE;
	    goto failure;
	}

	if (rrc) {
	    token->RRC[0] = (rrc >> 8) & 0xFF;
	    token->RRC[1] = (rrc >> 0) & 0xFF;
	}

    } else {
	/*
	  plain packet:

	  {data | "header" | gss-trailer (krb5 checksum)

	  don't do RRC != 0

	 */

	for (i = 0; i < iov_count; i++) {
	    switch (GSS_IOV_BUFFER_TYPE(iov[i].type)) {
	    case GSS_IOV_BUFFER_TYPE_DATA:
		data[i].flags = KRB5_CRYPTO_TYPE_DATA;
		break;
	    case GSS_IOV_BUFFER_TYPE_SIGN_ONLY:
		data[i].flags = KRB5_CRYPTO_TYPE_SIGN_ONLY;
		break;
	    default:
		data[i].flags = KRB5_CRYPTO_TYPE_EMPTY;
		break;
	    }
	    data[i].data.length = iov[i].buffer.length;
	    data[i].data.data = iov[i].buffer.value;
	}

	data[i].flags = KRB5_CRYPTO_TYPE_DATA;
	data[i].data.data = header->buffer.value;
	data[i].data.length = sizeof(gss_cfx_wrap_token_desc);
	i++;

	data[i].flags = KRB5_CRYPTO_TYPE_CHECKSUM;
	if (trailer) {
		data[i].data.data = trailer->buffer.value;
	} else {
		data[i].data.data = (uint8_t *)header->buffer.value +
				     sizeof(gss_cfx_wrap_token_desc);
	}
	data[i].data.length = k5tsize;
	i++;

	ret = krb5_create_checksum_iov(context, ctx->crypto, usage, data, i, NULL);
	if (ret) {
	    *minor_status = ret;
	    major_status = GSS_S_FAILURE;
	    goto failure;
	}

	if (rrc) {
	    token->RRC[0] = (rrc >> 8) & 0xFF;
	    token->RRC[1] = (rrc >> 0) & 0xFF;
	}

	token->EC[0] =  (k5tsize >> 8) & 0xFF;
	token->EC[1] =  (k5tsize >> 0) & 0xFF;
    }

    if (conf_state != NULL)
	*conf_state = conf_req_flag;

    free(data);

    *minor_status = 0;
    return GSS_S_COMPLETE;

 failure:
    if (data)
	free(data);

    gss_release_iov_buffer(&junk, iov, iov_count);

    return major_status;
}
#endif

/* This is slowpath */
static OM_uint32
unrotate_iov(OM_uint32 *minor_status, size_t rrc, gss_iov_buffer_desc *iov, int iov_count)
{
    uint8_t *p, *q;
    size_t len = 0, skip;
    int i;

    for (i = 0; i < iov_count; i++)
	if (GSS_IOV_BUFFER_TYPE(iov[i].type) == GSS_IOV_BUFFER_TYPE_DATA ||
	    GSS_IOV_BUFFER_TYPE(iov[i].type) == GSS_IOV_BUFFER_TYPE_PADDING ||
	    GSS_IOV_BUFFER_TYPE(iov[i].type) == GSS_IOV_BUFFER_TYPE_TRAILER)
	    len += iov[i].buffer.length;

    p = malloc(len);
    if (p == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }
    q = p;

    /* copy up */

    for (i = 0; i < iov_count; i++) {
	if (GSS_IOV_BUFFER_TYPE(iov[i].type) == GSS_IOV_BUFFER_TYPE_DATA ||
	    GSS_IOV_BUFFER_TYPE(iov[i].type) == GSS_IOV_BUFFER_TYPE_PADDING ||
	    GSS_IOV_BUFFER_TYPE(iov[i].type) == GSS_IOV_BUFFER_TYPE_TRAILER)
	{
	    memcpy(q, iov[i].buffer.value, iov[i].buffer.length);
	    q += iov[i].buffer.length;
	}
    }
    assert((size_t)(q - p) == len);

    /* unrotate first part */
    q = p + rrc;
    skip = rrc;
    for (i = 0; i < iov_count; i++) {
	if (GSS_IOV_BUFFER_TYPE(iov[i].type) == GSS_IOV_BUFFER_TYPE_DATA ||
	    GSS_IOV_BUFFER_TYPE(iov[i].type) == GSS_IOV_BUFFER_TYPE_PADDING ||
	    GSS_IOV_BUFFER_TYPE(iov[i].type) == GSS_IOV_BUFFER_TYPE_TRAILER)
	{
	    if (iov[i].buffer.length <= skip) {
		skip -= iov[i].buffer.length;
	    } else {
		memcpy(((uint8_t *)iov[i].buffer.value) + skip, q, iov[i].buffer.length - skip);
		q += iov[i].buffer.length - skip;
		skip = 0;
	    }
	}
    }
    /* copy trailer */
    q = p;
    skip = rrc;
    for (i = 0; i < iov_count; i++) {
	if (GSS_IOV_BUFFER_TYPE(iov[i].type) == GSS_IOV_BUFFER_TYPE_DATA ||
	    GSS_IOV_BUFFER_TYPE(iov[i].type) == GSS_IOV_BUFFER_TYPE_PADDING ||
	    GSS_IOV_BUFFER_TYPE(iov[i].type) == GSS_IOV_BUFFER_TYPE_TRAILER)
	{
	    memcpy(q, iov[i].buffer.value, min(iov[i].buffer.length, skip));
	    if (iov[i].buffer.length > skip)
		break;
	    skip -= iov[i].buffer.length;
	    q += iov[i].buffer.length;
	}
    }
    return GSS_S_COMPLETE;
}

#if 0

OM_uint32
_gssapi_unwrap_cfx_iov(OM_uint32 *minor_status,
		       gsskrb5_ctx ctx,
		       krb5_context context,
		       int *conf_state,
		       gss_qop_t *qop_state,
		       gss_iov_buffer_desc *iov,
		       int iov_count)
{
    OM_uint32 seq_number_lo, seq_number_hi, major_status, junk;
    gss_iov_buffer_desc *header, *trailer, *padding;
    gss_cfx_wrap_token token, ttoken;
    u_char token_flags;
    krb5_error_code ret;
    unsigned usage;
    uint16_t ec, rrc;
    krb5_crypto_iov *data = NULL;
    int i, j;

    *minor_status = 0;

    header = _gk_find_buffer(iov, iov_count, GSS_IOV_BUFFER_TYPE_HEADER);
    if (header == NULL) {
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    if (header->buffer.length < sizeof(*token)) /* we check exact below */
	return GSS_S_DEFECTIVE_TOKEN;

    padding = _gk_find_buffer(iov, iov_count, GSS_IOV_BUFFER_TYPE_PADDING);
    if (padding != NULL && padding->buffer.length != 0) {
	*minor_status = EINVAL;
	return GSS_S_FAILURE;
    }

    trailer = _gk_find_buffer(iov, iov_count, GSS_IOV_BUFFER_TYPE_TRAILER);

    major_status = _gk_verify_buffers(minor_status, ctx, header, padding, trailer);
    if (major_status != GSS_S_COMPLETE) {
	    return major_status;
    }

    token = (gss_cfx_wrap_token)header->buffer.value;

    if (token->TOK_ID[0] != 0x05 || token->TOK_ID[1] != 0x04)
	return GSS_S_DEFECTIVE_TOKEN;

    /* Ignore unknown flags */
    token_flags = token->Flags &
	(CFXSentByAcceptor | CFXSealed | CFXAcceptorSubkey);

    if (token_flags & CFXSentByAcceptor) {
	if ((ctx->more_flags & LOCAL) == 0)
	    return GSS_S_DEFECTIVE_TOKEN;
    }

    if (ctx->more_flags & ACCEPTOR_SUBKEY) {
	if ((token_flags & CFXAcceptorSubkey) == 0)
	    return GSS_S_DEFECTIVE_TOKEN;
    } else {
	if (token_flags & CFXAcceptorSubkey)
	    return GSS_S_DEFECTIVE_TOKEN;
    }

    if (token->Filler != 0xFF)
	return GSS_S_DEFECTIVE_TOKEN;

    if (conf_state != NULL)
	*conf_state = (token_flags & CFXSealed) ? 1 : 0;

    ec  = (token->EC[0]  << 8) | token->EC[1];
    rrc = (token->RRC[0] << 8) | token->RRC[1];

    /*
     * Check sequence number
     */
    _gsskrb5_decode_be_om_uint32(&token->SND_SEQ[0], &seq_number_hi);
    _gsskrb5_decode_be_om_uint32(&token->SND_SEQ[4], &seq_number_lo);
    if (seq_number_hi) {
	/* no support for 64-bit sequence numbers */
	*minor_status = ERANGE;
	return GSS_S_UNSEQ_TOKEN;
    }

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);
    ret = _gssapi_msg_order_check(ctx->order, seq_number_lo);
    if (ret != 0) {
	*minor_status = 0;
	HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	return ret;
    }
    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);

    /*
     * Decrypt and/or verify checksum
     */

    if (ctx->more_flags & LOCAL) {
	usage = KRB5_KU_USAGE_ACCEPTOR_SEAL;
    } else {
	usage = KRB5_KU_USAGE_INITIATOR_SEAL;
    }

    data = calloc(iov_count + 3, sizeof(data[0]));
    if (data == NULL) {
	*minor_status = ENOMEM;
	major_status = GSS_S_FAILURE;
	goto failure;
    }

    if (token_flags & CFXSealed) {
	size_t k5tsize, k5hsize;

	krb5_crypto_length(context, ctx->crypto, KRB5_CRYPTO_TYPE_HEADER, &k5hsize);
	krb5_crypto_length(context, ctx->crypto, KRB5_CRYPTO_TYPE_TRAILER, &k5tsize);

	/* Rotate by RRC; bogus to do this in-place XXX */
	/* Check RRC */

	if (trailer == NULL) {
	    size_t gsstsize = k5tsize + sizeof(*token);
	    size_t gsshsize = k5hsize + sizeof(*token);

	    if (rrc != gsstsize) {
		major_status = GSS_S_DEFECTIVE_TOKEN;
		goto failure;
	    }

	    if (IS_DCE_STYLE(ctx))
		gsstsize += ec;

	    gsshsize += gsstsize;

	    if (header->buffer.length != gsshsize) {
		major_status = GSS_S_DEFECTIVE_TOKEN;
		goto failure;
	    }
	} else if (trailer->buffer.length != sizeof(*token) + k5tsize) {
	    major_status = GSS_S_DEFECTIVE_TOKEN;
	    goto failure;
	} else if (header->buffer.length != sizeof(*token) + k5hsize) {
	    major_status = GSS_S_DEFECTIVE_TOKEN;
	    goto failure;
	} else if (rrc != 0) {
	    /* go though slowpath */
	    major_status = unrotate_iov(minor_status, rrc, iov, iov_count);
	    if (major_status)
		goto failure;
	}

	i = 0;
	data[i].flags = KRB5_CRYPTO_TYPE_HEADER;
	data[i].data.data = ((uint8_t *)header->buffer.value) + header->buffer.length - k5hsize;
	data[i].data.length = k5hsize;
	i++;

	for (j = 0; j < iov_count; i++, j++) {
	    switch (GSS_IOV_BUFFER_TYPE(iov[j].type)) {
	    case GSS_IOV_BUFFER_TYPE_DATA:
		data[i].flags = KRB5_CRYPTO_TYPE_DATA;
		break;
	    case GSS_IOV_BUFFER_TYPE_SIGN_ONLY:
		data[i].flags = KRB5_CRYPTO_TYPE_SIGN_ONLY;
		break;
	    default:
		data[i].flags = KRB5_CRYPTO_TYPE_EMPTY;
		break;
	    }
	    data[i].data.length = iov[j].buffer.length;
	    data[i].data.data = iov[j].buffer.value;
	}

	/* encrypted CFX header in trailer (or after the header if in
	   DCE mode). Copy in header into E"header"
	*/
	data[i].flags = KRB5_CRYPTO_TYPE_DATA;
	if (trailer) {
	    data[i].data.data = trailer->buffer.value;
	} else {
	    data[i].data.data = ((uint8_t *)header->buffer.value) +
		header->buffer.length - k5hsize - k5tsize - ec- sizeof(*token);
	}

	data[i].data.length = ec + sizeof(*token);
	ttoken = (gss_cfx_wrap_token)(((uint8_t *)data[i].data.data) + ec);
	i++;

	/* Kerberos trailer comes after the gss trailer */
	data[i].flags = KRB5_CRYPTO_TYPE_TRAILER;
	data[i].data.data = ((uint8_t *)data[i-1].data.data) + ec + sizeof(*token);
	data[i].data.length = k5tsize;
	i++;

	ret = krb5_decrypt_iov_ivec(context, ctx->crypto, usage, data, i, NULL);
	if (ret != 0) {
	    *minor_status = ret;
	    major_status = GSS_S_FAILURE;
	    goto failure;
	}

	ttoken->RRC[0] = token->RRC[0];
	ttoken->RRC[1] = token->RRC[1];

	/* Check the integrity of the header */
	if (ct_memcmp(ttoken, token, sizeof(*token)) != 0) {
	    major_status = GSS_S_BAD_MIC;
	    goto failure;
	}
    } else {
	size_t gsstsize = ec;
	size_t gsshsize = sizeof(*token);

	if (trailer == NULL) {
	    /* Check RRC */
	    if (rrc != gsstsize) {
	       *minor_status = EINVAL;
	       major_status = GSS_S_FAILURE;
	       goto failure;
	    }

	    gsshsize += gsstsize;
	    gsstsize = 0;
	} else if (trailer->buffer.length != gsstsize) {
	    major_status = GSS_S_DEFECTIVE_TOKEN;
	    goto failure;
	} else if (rrc != 0) {
	    /* Check RRC */
	    *minor_status = EINVAL;
	    major_status = GSS_S_FAILURE;
	    goto failure;
	}

	if (header->buffer.length != gsshsize) {
	    major_status = GSS_S_DEFECTIVE_TOKEN;
	    goto failure;
	}

	for (i = 0; i < iov_count; i++) {
	    switch (GSS_IOV_BUFFER_TYPE(iov[i].type)) {
	    case GSS_IOV_BUFFER_TYPE_DATA:
		data[i].flags = KRB5_CRYPTO_TYPE_DATA;
		break;
	    case GSS_IOV_BUFFER_TYPE_SIGN_ONLY:
		data[i].flags = KRB5_CRYPTO_TYPE_SIGN_ONLY;
		break;
	    default:
		data[i].flags = KRB5_CRYPTO_TYPE_EMPTY;
		break;
	    }
	    data[i].data.length = iov[i].buffer.length;
	    data[i].data.data = iov[i].buffer.value;
	}

	data[i].flags = KRB5_CRYPTO_TYPE_DATA;
	data[i].data.data = header->buffer.value;
	data[i].data.length = sizeof(*token);
	i++;

	data[i].flags = KRB5_CRYPTO_TYPE_CHECKSUM;
	if (trailer) {
		data[i].data.data = trailer->buffer.value;
	} else {
		data[i].data.data = (uint8_t *)header->buffer.value +
				     sizeof(*token);
	}
	data[i].data.length = ec;
	i++;

	token = (gss_cfx_wrap_token)header->buffer.value;
	token->EC[0]  = 0;
	token->EC[1]  = 0;
	token->RRC[0] = 0;
	token->RRC[1] = 0;

	ret = krb5_verify_checksum_iov(context, ctx->crypto, usage, data, i, NULL);
	if (ret) {
	    *minor_status = ret;
	    major_status = GSS_S_FAILURE;
	    goto failure;
	}
    }

    if (qop_state != NULL) {
	*qop_state = GSS_C_QOP_DEFAULT;
    }

    free(data);

    *minor_status = 0;
    return GSS_S_COMPLETE;

 failure:
    if (data)
	free(data);

    gss_release_iov_buffer(&junk, iov, iov_count);

    return major_status;
}
#endif

OM_uint32
_gssapi_wrap_iov_length_cfx(OM_uint32 *minor_status,
			    gsskrb5_ctx ctx,
			    krb5_context context,
			    int conf_req_flag,
			    gss_qop_t qop_req,
			    int *conf_state,
			    gss_iov_buffer_desc *iov,
			    int iov_count)
{
    OM_uint32 major_status;
    size_t size;
    int i;
    gss_iov_buffer_desc *header = NULL;
    gss_iov_buffer_desc *padding = NULL;
    gss_iov_buffer_desc *trailer = NULL;
    size_t gsshsize = 0;
    size_t gsstsize = 0;
    size_t k5hsize = 0;
    size_t k5tsize = 0;

    GSSAPI_KRB5_INIT (&context);
    *minor_status = 0;

    for (size = 0, i = 0; i < iov_count; i++) {
	switch(GSS_IOV_BUFFER_TYPE(iov[i].type)) {
	case GSS_IOV_BUFFER_TYPE_EMPTY:
	    break;
	case GSS_IOV_BUFFER_TYPE_DATA:
	    size += iov[i].buffer.length;
	    break;
	case GSS_IOV_BUFFER_TYPE_HEADER:
	    if (header != NULL) {
		*minor_status = 0;
		return GSS_S_FAILURE;
	    }
	    header = &iov[i];
	    break;
	case GSS_IOV_BUFFER_TYPE_TRAILER:
	    if (trailer != NULL) {
		*minor_status = 0;
		return GSS_S_FAILURE;
	    }
	    trailer = &iov[i];
	    break;
	case GSS_IOV_BUFFER_TYPE_PADDING:
	    if (padding != NULL) {
		*minor_status = 0;
		return GSS_S_FAILURE;
	    }
	    padding = &iov[i];
	    break;
	case GSS_IOV_BUFFER_TYPE_SIGN_ONLY:
	    break;
	default:
	    *minor_status = EINVAL;
	    return GSS_S_FAILURE;
	}
    }

    major_status = _gk_verify_buffers(minor_status, ctx, header, padding, trailer);
    if (major_status != GSS_S_COMPLETE) {
	    return major_status;
    }

    if (conf_req_flag) {
	size_t k5psize = 0;
	size_t k5pbase = 0;
	size_t k5bsize = 0;
	size_t ec = 0;

	size += sizeof(gss_cfx_wrap_token_desc);

	*minor_status = krb5_crypto_length(context, ctx->crypto,
					   KRB5_CRYPTO_TYPE_HEADER,
					   &k5hsize);
	if (*minor_status)
	    return GSS_S_FAILURE;

	*minor_status = krb5_crypto_length(context, ctx->crypto,
					   KRB5_CRYPTO_TYPE_TRAILER,
					   &k5tsize);
	if (*minor_status)
	    return GSS_S_FAILURE;

	*minor_status = krb5_crypto_length(context, ctx->crypto,
					   KRB5_CRYPTO_TYPE_PADDING,
					   &k5pbase);
	if (*minor_status)
	    return GSS_S_FAILURE;

	if (k5pbase > 1) {
	    k5psize = k5pbase - (size % k5pbase);
	} else {
	    k5psize = 0;
	}

	if (k5psize == 0 && IS_DCE_STYLE(ctx)) {
	    *minor_status = krb5_crypto_getblocksize(context, ctx->crypto,
						     &k5bsize);
	    if (*minor_status)
		return GSS_S_FAILURE;

	    ec = k5bsize;
	} else {
	    ec = k5psize;
	}

	gsshsize = sizeof(gss_cfx_wrap_token_desc) + k5hsize;
	gsstsize = sizeof(gss_cfx_wrap_token_desc) + ec + k5tsize;
    } else {
	*minor_status = krb5_crypto_length(context, ctx->crypto,
					   KRB5_CRYPTO_TYPE_CHECKSUM,
					   &k5tsize);
	if (*minor_status)
	    return GSS_S_FAILURE;

	gsshsize = sizeof(gss_cfx_wrap_token_desc);
	gsstsize = k5tsize;
    }

    if (trailer != NULL) {
	trailer->buffer.length = gsstsize;
    } else {
	gsshsize += gsstsize;
    }

    header->buffer.length = gsshsize;

    if (padding) {
	/* padding is done via EC and is contained in the header or trailer */
	padding->buffer.length = 0;
    }

    if (conf_state) {
	*conf_state = conf_req_flag;
    }

    return GSS_S_COMPLETE;
}




OM_uint32 _gssapi_wrap_cfx(OM_uint32 *minor_status,
			   const gsskrb5_ctx ctx,
			   krb5_context context,
			   int conf_req_flag,
			   const gss_buffer_t input_message_buffer,
			   int *conf_state,
			   gss_buffer_t output_message_buffer)
{
    gss_cfx_wrap_token token;
    krb5_error_code ret;
    unsigned usage;
    krb5_data cipher;
    size_t wrapped_len, cksumsize;
    uint16_t padlength, rrc = 0;
    int32_t seq_number;
    u_char *p;

    ret = _gsskrb5cfx_wrap_length_cfx(context,
				      ctx->crypto, conf_req_flag,
				      IS_DCE_STYLE(ctx),
				      input_message_buffer->length,
				      &wrapped_len, &cksumsize, &padlength);
    if (ret != 0) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    /* Always rotate encrypted token (if any) and checksum to header */
    rrc = (conf_req_flag ? sizeof(*token) : 0) + (uint16_t)cksumsize;

    output_message_buffer->length = wrapped_len;
    output_message_buffer->value = malloc(output_message_buffer->length);
    if (output_message_buffer->value == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    p = output_message_buffer->value;
    token = (gss_cfx_wrap_token)p;
    token->TOK_ID[0] = 0x05;
    token->TOK_ID[1] = 0x04;
    token->Flags     = 0;
    token->Filler    = 0xFF;
    if ((ctx->more_flags & LOCAL) == 0)
	token->Flags |= CFXSentByAcceptor;
    if (ctx->more_flags & ACCEPTOR_SUBKEY)
	token->Flags |= CFXAcceptorSubkey;
    if (conf_req_flag) {
	/*
	 * In Wrap tokens with confidentiality, the EC field is
	 * used to encode the size (in bytes) of the random filler.
	 */
	token->Flags |= CFXSealed;
	token->EC[0] = (padlength >> 8) & 0xFF;
	token->EC[1] = (padlength >> 0) & 0xFF;
    } else {
	/*
	 * In Wrap tokens without confidentiality, the EC field is
	 * used to encode the size (in bytes) of the trailing
	 * checksum.
	 *
	 * This is not used in the checksum calcuation itself,
	 * because the checksum length could potentially vary
	 * depending on the data length.
	 */
	token->EC[0] = 0;
	token->EC[1] = 0;
    }

    /*
     * In Wrap tokens that provide for confidentiality, the RRC
     * field in the header contains the hex value 00 00 before
     * encryption.
     *
     * In Wrap tokens that do not provide for confidentiality,
     * both the EC and RRC fields in the appended checksum
     * contain the hex value 00 00 for the purpose of calculating
     * the checksum.
     */
    token->RRC[0] = 0;
    token->RRC[1] = 0;

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);
    krb5_auth_con_getlocalseqnumber(context,
				    ctx->auth_context,
				    &seq_number);
    _gsskrb5_encode_be_om_uint32(0,          &token->SND_SEQ[0]);
    _gsskrb5_encode_be_om_uint32(seq_number, &token->SND_SEQ[4]);
    krb5_auth_con_setlocalseqnumber(context,
				    ctx->auth_context,
				    ++seq_number);
    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);

    /*
     * If confidentiality is requested, the token header is
     * appended to the plaintext before encryption; the resulting
     * token is {"header" | encrypt(plaintext | pad | "header")}.
     *
     * If no confidentiality is requested, the checksum is
     * calculated over the plaintext concatenated with the
     * token header.
     */
    if (ctx->more_flags & LOCAL) {
	usage = KRB5_KU_USAGE_INITIATOR_SEAL;
    } else {
	usage = KRB5_KU_USAGE_ACCEPTOR_SEAL;
    }

    if (conf_req_flag) {
	/*
	 * Any necessary padding is added here to ensure that the
	 * encrypted token header is always at the end of the
	 * ciphertext.
	 *
	 * The specification does not require that the padding
	 * bytes are initialized.
	 */
	p += sizeof(*token);
	memcpy(p, input_message_buffer->value, input_message_buffer->length);
	memset(p + input_message_buffer->length, 0xFF, padlength);
	memcpy(p + input_message_buffer->length + padlength,
	       token, sizeof(*token));

	ret = krb5_encrypt(context, ctx->crypto,
			   usage, p,
			   input_message_buffer->length + padlength +
				sizeof(*token),
			   &cipher);
	if (ret != 0) {
	    *minor_status = ret;
	    _gsskrb5_release_buffer(minor_status, output_message_buffer);
	    return GSS_S_FAILURE;
	}
	assert(sizeof(*token) + cipher.length == wrapped_len);
	token->RRC[0] = (rrc >> 8) & 0xFF;
	token->RRC[1] = (rrc >> 0) & 0xFF;

	/*
	 * this is really ugly, but needed against windows
	 * for DCERPC, as windows rotates by EC+RRC.
	 */
	if (IS_DCE_STYLE(ctx)) {
		ret = rrc_rotate(cipher.data, cipher.length, rrc+padlength, FALSE);
	} else {
		ret = rrc_rotate(cipher.data, cipher.length, rrc, FALSE);
	}
	if (ret != 0) {
	    *minor_status = ret;
	    _gsskrb5_release_buffer(minor_status, output_message_buffer);
	    return GSS_S_FAILURE;
	}
	memcpy(p, cipher.data, cipher.length);
	krb5_data_free(&cipher);
    } else {
	char *buf;
	Checksum cksum;

	buf = malloc(input_message_buffer->length + sizeof(*token));
	if (buf == NULL) {
	    *minor_status = ENOMEM;
	    _gsskrb5_release_buffer(minor_status, output_message_buffer);
	    return GSS_S_FAILURE;
	}
	memcpy(buf, input_message_buffer->value, input_message_buffer->length);
	memcpy(buf + input_message_buffer->length, token, sizeof(*token));

	ret = krb5_create_checksum(context, ctx->crypto,
				   usage, 0, buf,
				   input_message_buffer->length +
					sizeof(*token),
				   &cksum);
	if (ret != 0) {
	    *minor_status = ret;
	    _gsskrb5_release_buffer(minor_status, output_message_buffer);
	    free(buf);
	    return GSS_S_FAILURE;
	}

	free(buf);

	assert(cksum.checksum.length == cksumsize);
	token->EC[0] =  (cksum.checksum.length >> 8) & 0xFF;
	token->EC[1] =  (cksum.checksum.length >> 0) & 0xFF;
	token->RRC[0] = (rrc >> 8) & 0xFF;
	token->RRC[1] = (rrc >> 0) & 0xFF;

	p += sizeof(*token);
	memcpy(p, input_message_buffer->value, input_message_buffer->length);
	memcpy(p + input_message_buffer->length,
	       cksum.checksum.data, cksum.checksum.length);

	ret = rrc_rotate(p,
	    input_message_buffer->length + cksum.checksum.length, rrc, FALSE);
	if (ret != 0) {
	    *minor_status = ret;
	    _gsskrb5_release_buffer(minor_status, output_message_buffer);
	    free_Checksum(&cksum);
	    return GSS_S_FAILURE;
	}
	free_Checksum(&cksum);
    }

    if (conf_state != NULL) {
	*conf_state = conf_req_flag;
    }

    *minor_status = 0;
    return GSS_S_COMPLETE;
}

OM_uint32 _gssapi_unwrap_cfx(OM_uint32 *minor_status,
			     const gsskrb5_ctx ctx,
			     krb5_context context,
			     const gss_buffer_t input_message_buffer,
			     gss_buffer_t output_message_buffer,
			     int *conf_state,
			     gss_qop_t *qop_state)
{
    gss_cfx_wrap_token token;
    u_char token_flags;
    krb5_error_code ret;
    unsigned usage;
    krb5_data data;
    uint16_t ec, rrc;
    OM_uint32 seq_number_lo, seq_number_hi;
    size_t len;
    u_char *p;

    *minor_status = 0;

    if (input_message_buffer->length < sizeof(*token)) {
	return GSS_S_DEFECTIVE_TOKEN;
    }

    p = input_message_buffer->value;

    token = (gss_cfx_wrap_token)p;

    if (token->TOK_ID[0] != 0x05 || token->TOK_ID[1] != 0x04) {
	return GSS_S_DEFECTIVE_TOKEN;
    }

    /* Ignore unknown flags */
    token_flags = token->Flags &
	(CFXSentByAcceptor | CFXSealed | CFXAcceptorSubkey);

    if (token_flags & CFXSentByAcceptor) {
	if ((ctx->more_flags & LOCAL) == 0)
	    return GSS_S_DEFECTIVE_TOKEN;
    }

    if (ctx->more_flags & ACCEPTOR_SUBKEY) {
	if ((token_flags & CFXAcceptorSubkey) == 0)
	    return GSS_S_DEFECTIVE_TOKEN;
    } else {
	if (token_flags & CFXAcceptorSubkey)
	    return GSS_S_DEFECTIVE_TOKEN;
    }

    if (token->Filler != 0xFF) {
	return GSS_S_DEFECTIVE_TOKEN;
    }

    if (conf_state != NULL) {
	*conf_state = (token_flags & CFXSealed) ? 1 : 0;
    }

    ec  = (token->EC[0]  << 8) | token->EC[1];
    rrc = (token->RRC[0] << 8) | token->RRC[1];

    /*
     * Check sequence number
     */
    _gsskrb5_decode_be_om_uint32(&token->SND_SEQ[0], &seq_number_hi);
    _gsskrb5_decode_be_om_uint32(&token->SND_SEQ[4], &seq_number_lo);
    if (seq_number_hi) {
	/* no support for 64-bit sequence numbers */
	*minor_status = ERANGE;
	return GSS_S_UNSEQ_TOKEN;
    }

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);
    ret = _gssapi_msg_order_check(ctx->order, seq_number_lo);
    if (ret != 0) {
	*minor_status = 0;
	HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	_gsskrb5_release_buffer(minor_status, output_message_buffer);
	return ret;
    }
    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);

    /*
     * Decrypt and/or verify checksum
     */

    if (ctx->more_flags & LOCAL) {
	usage = KRB5_KU_USAGE_ACCEPTOR_SEAL;
    } else {
	usage = KRB5_KU_USAGE_INITIATOR_SEAL;
    }

    p += sizeof(*token);
    len = input_message_buffer->length;
    len -= (p - (u_char *)input_message_buffer->value);

    if (token_flags & CFXSealed) {
	/*
	 * this is really ugly, but needed against windows
	 * for DCERPC, as windows rotates by EC+RRC.
	 */
	if (IS_DCE_STYLE(ctx)) {
		*minor_status = rrc_rotate(p, len, rrc+ec, TRUE);
	} else {
		*minor_status = rrc_rotate(p, len, rrc, TRUE);
	}
	if (*minor_status != 0) {
	    return GSS_S_FAILURE;
	}

	ret = krb5_decrypt(context, ctx->crypto, usage,
	    p, len, &data);
	if (ret != 0) {
	    *minor_status = ret;
	    return GSS_S_BAD_MIC;
	}

	/* Check that there is room for the pad and token header */
	if (data.length < ec + sizeof(*token)) {
	    krb5_data_free(&data);
	    return GSS_S_DEFECTIVE_TOKEN;
	}
	p = data.data;
	p += data.length - sizeof(*token);

	/* RRC is unprotected; don't modify input buffer */
	((gss_cfx_wrap_token)p)->RRC[0] = token->RRC[0];
	((gss_cfx_wrap_token)p)->RRC[1] = token->RRC[1];

	/* Check the integrity of the header */
	if (ct_memcmp(p, token, sizeof(*token)) != 0) {
	    krb5_data_free(&data);
	    return GSS_S_BAD_MIC;
	}

	output_message_buffer->value = data.data;
	output_message_buffer->length = data.length - ec - sizeof(*token);
    } else {
	Checksum cksum;

	/* Rotate by RRC; bogus to do this in-place XXX */
	*minor_status = rrc_rotate(p, len, rrc, TRUE);
	if (*minor_status != 0) {
	    return GSS_S_FAILURE;
	}

	/* Determine checksum type */
	ret = krb5_crypto_get_checksum_type(context,
					    ctx->crypto,
					    &cksum.cksumtype);
	if (ret != 0) {
	    *minor_status = ret;
	    return GSS_S_FAILURE;
	}

	cksum.checksum.length = ec;

	/* Check we have at least as much data as the checksum */
	if (len < cksum.checksum.length) {
	    *minor_status = ERANGE;
	    return GSS_S_BAD_MIC;
	}

	/* Length now is of the plaintext only, no checksum */
	len -= cksum.checksum.length;
	cksum.checksum.data = p + len;

	output_message_buffer->length = len; /* for later */
	output_message_buffer->value = malloc(len + sizeof(*token));
	if (output_message_buffer->value == NULL) {
	    *minor_status = ENOMEM;
	    return GSS_S_FAILURE;
	}

	/* Checksum is over (plaintext-data | "header") */
	memcpy(output_message_buffer->value, p, len);
	memcpy((u_char *)output_message_buffer->value + len,
	       token, sizeof(*token));

	/* EC is not included in checksum calculation */
	token = (gss_cfx_wrap_token)((u_char *)output_message_buffer->value +
				     len);
	token->EC[0]  = 0;
	token->EC[1]  = 0;
	token->RRC[0] = 0;
	token->RRC[1] = 0;

	ret = krb5_verify_checksum(context, ctx->crypto,
				   usage,
				   output_message_buffer->value,
				   len + sizeof(*token),
				   &cksum);
	if (ret != 0) {
	    *minor_status = ret;
	    _gsskrb5_release_buffer(minor_status, output_message_buffer);
	    return GSS_S_BAD_MIC;
	}
    }

    if (qop_state != NULL) {
	*qop_state = GSS_C_QOP_DEFAULT;
    }

    *minor_status = 0;
    return GSS_S_COMPLETE;
}

OM_uint32 _gssapi_mic_cfx(OM_uint32 *minor_status,
			  const gsskrb5_ctx ctx,
			  krb5_context context,
			  gss_qop_t qop_req,
			  const gss_buffer_t message_buffer,
			  gss_buffer_t message_token)
{
    gss_cfx_mic_token token;
    krb5_error_code ret;
    unsigned usage;
    Checksum cksum;
    u_char *buf;
    size_t len;
    int32_t seq_number;

    len = message_buffer->length + sizeof(*token);
    buf = malloc(len);
    if (buf == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    memcpy(buf, message_buffer->value, message_buffer->length);

    token = (gss_cfx_mic_token)(buf + message_buffer->length);
    token->TOK_ID[0] = 0x04;
    token->TOK_ID[1] = 0x04;
    token->Flags = 0;
    if ((ctx->more_flags & LOCAL) == 0)
	token->Flags |= CFXSentByAcceptor;
    if (ctx->more_flags & ACCEPTOR_SUBKEY)
	token->Flags |= CFXAcceptorSubkey;
    memset(token->Filler, 0xFF, 5);

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);
    krb5_auth_con_getlocalseqnumber(context,
				    ctx->auth_context,
				    &seq_number);
    _gsskrb5_encode_be_om_uint32(0,          &token->SND_SEQ[0]);
    _gsskrb5_encode_be_om_uint32(seq_number, &token->SND_SEQ[4]);
    krb5_auth_con_setlocalseqnumber(context,
				    ctx->auth_context,
				    ++seq_number);
    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);

    if (ctx->more_flags & LOCAL) {
	usage = KRB5_KU_USAGE_INITIATOR_SIGN;
    } else {
	usage = KRB5_KU_USAGE_ACCEPTOR_SIGN;
    }

    ret = krb5_create_checksum(context, ctx->crypto,
	usage, 0, buf, len, &cksum);
    if (ret != 0) {
	*minor_status = ret;
	free(buf);
	return GSS_S_FAILURE;
    }

    /* Determine MIC length */
    message_token->length = sizeof(*token) + cksum.checksum.length;
    message_token->value = malloc(message_token->length);
    if (message_token->value == NULL) {
	*minor_status = ENOMEM;
	free_Checksum(&cksum);
	free(buf);
	return GSS_S_FAILURE;
    }

    /* Token is { "header" | get_mic("header" | plaintext-data) } */
    memcpy(message_token->value, token, sizeof(*token));
    memcpy((u_char *)message_token->value + sizeof(*token),
	   cksum.checksum.data, cksum.checksum.length);

    free_Checksum(&cksum);
    free(buf);

    *minor_status = 0;
    return GSS_S_COMPLETE;
}

OM_uint32 _gssapi_verify_mic_cfx(OM_uint32 *minor_status,
				 const gsskrb5_ctx ctx,
				 krb5_context context,
				 const gss_buffer_t message_buffer,
				 const gss_buffer_t token_buffer,
				 gss_qop_t *qop_state)
{
    gss_cfx_mic_token token;
    u_char token_flags;
    krb5_error_code ret;
    unsigned usage;
    OM_uint32 seq_number_lo, seq_number_hi;
    u_char *buf, *p;
    Checksum cksum;

    *minor_status = 0;

    if (token_buffer->length < sizeof(*token)) {
	return GSS_S_DEFECTIVE_TOKEN;
    }

    p = token_buffer->value;

    token = (gss_cfx_mic_token)p;

    if (token->TOK_ID[0] != 0x04 || token->TOK_ID[1] != 0x04) {
	return GSS_S_DEFECTIVE_TOKEN;
    }

    /* Ignore unknown flags */
    token_flags = token->Flags & (CFXSentByAcceptor | CFXAcceptorSubkey);

    if (token_flags & CFXSentByAcceptor) {
	if ((ctx->more_flags & LOCAL) == 0)
	    return GSS_S_DEFECTIVE_TOKEN;
    }
    if (ctx->more_flags & ACCEPTOR_SUBKEY) {
	if ((token_flags & CFXAcceptorSubkey) == 0)
	    return GSS_S_DEFECTIVE_TOKEN;
    } else {
	if (token_flags & CFXAcceptorSubkey)
	    return GSS_S_DEFECTIVE_TOKEN;
    }

    if (ct_memcmp(token->Filler, "\xff\xff\xff\xff\xff", 5) != 0) {
	return GSS_S_DEFECTIVE_TOKEN;
    }

    /*
     * Check sequence number
     */
    _gsskrb5_decode_be_om_uint32(&token->SND_SEQ[0], &seq_number_hi);
    _gsskrb5_decode_be_om_uint32(&token->SND_SEQ[4], &seq_number_lo);
    if (seq_number_hi) {
	*minor_status = ERANGE;
	return GSS_S_UNSEQ_TOKEN;
    }

    HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);
    ret = _gssapi_msg_order_check(ctx->order, seq_number_lo);
    if (ret != 0) {
	*minor_status = 0;
	HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
	return ret;
    }
    HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);

    /*
     * Verify checksum
     */
    ret = krb5_crypto_get_checksum_type(context, ctx->crypto,
					&cksum.cksumtype);
    if (ret != 0) {
	*minor_status = ret;
	return GSS_S_FAILURE;
    }

    cksum.checksum.data = p + sizeof(*token);
    cksum.checksum.length = token_buffer->length - sizeof(*token);

    if (ctx->more_flags & LOCAL) {
	usage = KRB5_KU_USAGE_ACCEPTOR_SIGN;
    } else {
	usage = KRB5_KU_USAGE_INITIATOR_SIGN;
    }

    buf = malloc(message_buffer->length + sizeof(*token));
    if (buf == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }
    memcpy(buf, message_buffer->value, message_buffer->length);
    memcpy(buf + message_buffer->length, token, sizeof(*token));

    ret = krb5_verify_checksum(context, ctx->crypto,
			       usage,
			       buf,
			       sizeof(*token) + message_buffer->length,
			       &cksum);
    if (ret != 0) {
	*minor_status = ret;
	free(buf);
	return GSS_S_BAD_MIC;
    }

    free(buf);

    if (qop_state != NULL) {
	*qop_state = GSS_C_QOP_DEFAULT;
    }

    return GSS_S_COMPLETE;
}
