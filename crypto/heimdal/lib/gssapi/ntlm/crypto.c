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

#include "ntlm.h"

uint32_t
_krb5_crc_update (const char *p, size_t len, uint32_t res);
void
_krb5_crc_init_table(void);

/*
 *
 */

static void
encode_le_uint32(uint32_t n, unsigned char *p)
{
  p[0] = (n >> 0)  & 0xFF;
  p[1] = (n >> 8)  & 0xFF;
  p[2] = (n >> 16) & 0xFF;
  p[3] = (n >> 24) & 0xFF;
}


static void
decode_le_uint32(const void *ptr, uint32_t *n)
{
    const unsigned char *p = ptr;
    *n = (p[0] << 0) | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

/*
 *
 */

const char a2i_signmagic[] =
    "session key to server-to-client signing key magic constant";
const char a2i_sealmagic[] =
    "session key to server-to-client sealing key magic constant";
const char i2a_signmagic[] =
    "session key to client-to-server signing key magic constant";
const char i2a_sealmagic[] =
    "session key to client-to-server sealing key magic constant";


void
_gss_ntlm_set_key(struct ntlmv2_key *key, int acceptor, int sealsign,
		  unsigned char *data, size_t len)
{
    unsigned char out[16];
    EVP_MD_CTX *ctx;
    const char *signmagic;
    const char *sealmagic;

    if (acceptor) {
	signmagic = a2i_signmagic;
	sealmagic = a2i_sealmagic;
    } else {
	signmagic = i2a_signmagic;
	sealmagic = i2a_sealmagic;
    }

    key->seq = 0;

    ctx = EVP_MD_CTX_create();
    EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestUpdate(ctx, signmagic, strlen(signmagic) + 1);
    EVP_DigestFinal_ex(ctx, key->signkey, NULL);

    EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestUpdate(ctx, sealmagic, strlen(sealmagic) + 1);
    EVP_DigestFinal_ex(ctx, out, NULL);
    EVP_MD_CTX_destroy(ctx);

    RC4_set_key(&key->sealkey, 16, out);
    if (sealsign)
	key->signsealkey = &key->sealkey;
}

/*
 *
 */

static OM_uint32
v1_sign_message(gss_buffer_t in,
		RC4_KEY *signkey,
		uint32_t seq,
		unsigned char out[16])
{
    unsigned char sigature[12];
    uint32_t crc;

    _krb5_crc_init_table();
    crc = _krb5_crc_update(in->value, in->length, 0);

    encode_le_uint32(0, &sigature[0]);
    encode_le_uint32(crc, &sigature[4]);
    encode_le_uint32(seq, &sigature[8]);

    encode_le_uint32(1, out); /* version */
    RC4(signkey, sizeof(sigature), sigature, out + 4);

    if (RAND_bytes(out + 4, 4) != 1)
	return GSS_S_UNAVAILABLE;

    return 0;
}


static OM_uint32
v2_sign_message(gss_buffer_t in,
		unsigned char signkey[16],
		RC4_KEY *sealkey,
		uint32_t seq,
		unsigned char out[16])
{
    unsigned char hmac[16];
    unsigned int hmaclen;
    HMAC_CTX *c;

    c = HMAC_CTX_new();
    if (c == NULL)
	return GSS_S_FAILURE;
    HMAC_Init_ex(c, signkey, 16, EVP_md5(), NULL);

    encode_le_uint32(seq, hmac);
    HMAC_Update(c, hmac, 4);
    HMAC_Update(c, in->value, in->length);
    HMAC_Final(c, hmac, &hmaclen);
    HMAC_CTX_free(c);

    encode_le_uint32(1, &out[0]);
    if (sealkey)
	RC4(sealkey, 8, hmac, &out[4]);
    else
	memcpy(&out[4], hmac, 8);

    memset(&out[12], 0, 4);

    return GSS_S_COMPLETE;
}

static OM_uint32
v2_verify_message(gss_buffer_t in,
		  unsigned char signkey[16],
		  RC4_KEY *sealkey,
		  uint32_t seq,
		  const unsigned char checksum[16])
{
    OM_uint32 ret;
    unsigned char out[16];

    ret = v2_sign_message(in, signkey, sealkey, seq, out);
    if (ret)
	return ret;

    if (memcmp(checksum, out, 16) != 0)
	return GSS_S_BAD_MIC;

    return GSS_S_COMPLETE;
}

static OM_uint32
v2_seal_message(const gss_buffer_t in,
		unsigned char signkey[16],
		uint32_t seq,
		RC4_KEY *sealkey,
		gss_buffer_t out)
{
    unsigned char *p;
    OM_uint32 ret;

    if (in->length + 16 < in->length)
	return EINVAL;

    p = malloc(in->length + 16);
    if (p == NULL)
	return ENOMEM;

    RC4(sealkey, in->length, in->value, p);

    ret = v2_sign_message(in, signkey, sealkey, seq, &p[in->length]);
    if (ret) {
	free(p);
	return ret;
    }

    out->value = p;
    out->length = in->length + 16;

    return 0;
}

static OM_uint32
v2_unseal_message(gss_buffer_t in,
		  unsigned char signkey[16],
		  uint32_t seq,
		  RC4_KEY *sealkey,
		  gss_buffer_t out)
{
    OM_uint32 ret;

    if (in->length < 16)
	return GSS_S_BAD_MIC;

    out->length = in->length - 16;
    out->value = malloc(out->length);
    if (out->value == NULL)
	return GSS_S_BAD_MIC;

    RC4(sealkey, out->length, in->value, out->value);

    ret = v2_verify_message(out, signkey, sealkey, seq,
			    ((const unsigned char *)in->value) + out->length);
    if (ret) {
	OM_uint32 junk;
	gss_release_buffer(&junk, out);
    }
    return ret;
}

/*
 *
 */

#define CTX_FLAGS_ISSET(_ctx,_flags) \
    (((_ctx)->flags & (_flags)) == (_flags))

/*
 *
 */

OM_uint32 GSSAPI_CALLCONV
_gss_ntlm_get_mic
           (OM_uint32 * minor_status,
            const gss_ctx_id_t context_handle,
            gss_qop_t qop_req,
            const gss_buffer_t message_buffer,
            gss_buffer_t message_token
           )
{
    ntlm_ctx ctx = (ntlm_ctx)context_handle;
    OM_uint32 junk;

    *minor_status = 0;

    message_token->value = malloc(16);
    message_token->length = 16;
    if (message_token->value == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    if (CTX_FLAGS_ISSET(ctx, NTLM_NEG_SIGN|NTLM_NEG_NTLM2_SESSION)) {
	OM_uint32 ret;

	if ((ctx->status & STATUS_SESSIONKEY) == 0) {
	    gss_release_buffer(&junk, message_token);
	    return GSS_S_UNAVAILABLE;
	}

	ret = v2_sign_message(message_buffer,
			      ctx->u.v2.send.signkey,
			      ctx->u.v2.send.signsealkey,
			      ctx->u.v2.send.seq++,
			      message_token->value);
	if (ret)
	    gss_release_buffer(&junk, message_token);
        return ret;

    } else if (CTX_FLAGS_ISSET(ctx, NTLM_NEG_SIGN)) {
	OM_uint32 ret;

	if ((ctx->status & STATUS_SESSIONKEY) == 0) {
	    gss_release_buffer(&junk, message_token);
	    return GSS_S_UNAVAILABLE;
	}

	ret = v1_sign_message(message_buffer,
			      &ctx->u.v1.crypto_send.key,
			      ctx->u.v1.crypto_send.seq++,
			      message_token->value);
	if (ret)
	    gss_release_buffer(&junk, message_token);
        return ret;

    } else if (CTX_FLAGS_ISSET(ctx, NTLM_NEG_ALWAYS_SIGN)) {
	unsigned char *sigature;

	sigature = message_token->value;

	encode_le_uint32(1, &sigature[0]); /* version */
	encode_le_uint32(0, &sigature[4]);
	encode_le_uint32(0, &sigature[8]);
	encode_le_uint32(0, &sigature[12]);

        return GSS_S_COMPLETE;
    }
    gss_release_buffer(&junk, message_token);

    return GSS_S_UNAVAILABLE;
}

/*
 *
 */

OM_uint32 GSSAPI_CALLCONV
_gss_ntlm_verify_mic
           (OM_uint32 * minor_status,
            const gss_ctx_id_t context_handle,
            const gss_buffer_t message_buffer,
            const gss_buffer_t token_buffer,
            gss_qop_t * qop_state
	    )
{
    ntlm_ctx ctx = (ntlm_ctx)context_handle;

    if (qop_state != NULL)
	*qop_state = GSS_C_QOP_DEFAULT;
    *minor_status = 0;

    if (token_buffer->length != 16)
	return GSS_S_BAD_MIC;

    if (CTX_FLAGS_ISSET(ctx, NTLM_NEG_SIGN|NTLM_NEG_NTLM2_SESSION)) {
	OM_uint32 ret;

	if ((ctx->status & STATUS_SESSIONKEY) == 0)
	    return GSS_S_UNAVAILABLE;

	ret = v2_verify_message(message_buffer,
				ctx->u.v2.recv.signkey,
				ctx->u.v2.recv.signsealkey,
				ctx->u.v2.recv.seq++,
				token_buffer->value);
	if (ret)
	    return ret;

	return GSS_S_COMPLETE;
    } else if (CTX_FLAGS_ISSET(ctx, NTLM_NEG_SIGN)) {

	unsigned char sigature[12];
	uint32_t crc, num;

	if ((ctx->status & STATUS_SESSIONKEY) == 0)
	    return GSS_S_UNAVAILABLE;

	decode_le_uint32(token_buffer->value, &num);
	if (num != 1)
	    return GSS_S_BAD_MIC;

	RC4(&ctx->u.v1.crypto_recv.key, sizeof(sigature),
	    ((unsigned char *)token_buffer->value) + 4, sigature);

	_krb5_crc_init_table();
	crc = _krb5_crc_update(message_buffer->value,
			       message_buffer->length, 0);
	/* skip first 4 bytes in the encrypted checksum */
	decode_le_uint32(&sigature[4], &num);
	if (num != crc)
	    return GSS_S_BAD_MIC;
	decode_le_uint32(&sigature[8], &num);
	if (ctx->u.v1.crypto_recv.seq != num)
	    return GSS_S_BAD_MIC;
	ctx->u.v1.crypto_recv.seq++;

        return GSS_S_COMPLETE;
    } else if (ctx->flags & NTLM_NEG_ALWAYS_SIGN) {
	uint32_t num;
	unsigned char *p;

	p = (unsigned char*)(token_buffer->value);

	decode_le_uint32(&p[0], &num); /* version */
	if (num != 1) return GSS_S_BAD_MIC;
	decode_le_uint32(&p[4], &num);
	if (num != 0) return GSS_S_BAD_MIC;
	decode_le_uint32(&p[8], &num);
	if (num != 0) return GSS_S_BAD_MIC;
	decode_le_uint32(&p[12], &num);
	if (num != 0) return GSS_S_BAD_MIC;

        return GSS_S_COMPLETE;
    }

    return GSS_S_UNAVAILABLE;
}

/*
 *
 */

OM_uint32 GSSAPI_CALLCONV
_gss_ntlm_wrap_size_limit (
            OM_uint32 * minor_status,
            const gss_ctx_id_t context_handle,
            int conf_req_flag,
            gss_qop_t qop_req,
            OM_uint32 req_output_size,
            OM_uint32 * max_input_size
           )
{
    ntlm_ctx ctx = (ntlm_ctx)context_handle;

    *minor_status = 0;

    if(ctx->flags & NTLM_NEG_SEAL) {

	if (req_output_size < 16)
	    *max_input_size = 0;
	else
	    *max_input_size = req_output_size - 16;

	return GSS_S_COMPLETE;
    }

    return GSS_S_UNAVAILABLE;
}

/*
 *
 */

OM_uint32 GSSAPI_CALLCONV
_gss_ntlm_wrap
(OM_uint32 * minor_status,
 const gss_ctx_id_t context_handle,
 int conf_req_flag,
 gss_qop_t qop_req,
 const gss_buffer_t input_message_buffer,
 int * conf_state,
 gss_buffer_t output_message_buffer
    )
{
    ntlm_ctx ctx = (ntlm_ctx)context_handle;
    OM_uint32 ret;

    *minor_status = 0;
    if (conf_state)
	*conf_state = 0;
    if (output_message_buffer == GSS_C_NO_BUFFER)
	return GSS_S_FAILURE;


    if (CTX_FLAGS_ISSET(ctx, NTLM_NEG_SEAL|NTLM_NEG_NTLM2_SESSION)) {

	return v2_seal_message(input_message_buffer,
			       ctx->u.v2.send.signkey,
			       ctx->u.v2.send.seq++,
			       &ctx->u.v2.send.sealkey,
			       output_message_buffer);

    } else if (CTX_FLAGS_ISSET(ctx, NTLM_NEG_SEAL)) {
	gss_buffer_desc trailer;
	OM_uint32 junk;

	output_message_buffer->length = input_message_buffer->length + 16;
	output_message_buffer->value = malloc(output_message_buffer->length);
	if (output_message_buffer->value == NULL) {
	    output_message_buffer->length = 0;
	    return GSS_S_FAILURE;
	}


	RC4(&ctx->u.v1.crypto_send.key, input_message_buffer->length,
	    input_message_buffer->value, output_message_buffer->value);

	ret = _gss_ntlm_get_mic(minor_status, context_handle,
				0, input_message_buffer,
				&trailer);
	if (ret) {
	    gss_release_buffer(&junk, output_message_buffer);
	    return ret;
	}
	if (trailer.length != 16) {
	    gss_release_buffer(&junk, output_message_buffer);
	    gss_release_buffer(&junk, &trailer);
	    return GSS_S_FAILURE;
	}
	memcpy(((unsigned char *)output_message_buffer->value) +
	       input_message_buffer->length,
	       trailer.value, trailer.length);
	gss_release_buffer(&junk, &trailer);

	return GSS_S_COMPLETE;
    }

    return GSS_S_UNAVAILABLE;
}

/*
 *
 */

OM_uint32 GSSAPI_CALLCONV
_gss_ntlm_unwrap
           (OM_uint32 * minor_status,
            const gss_ctx_id_t context_handle,
            const gss_buffer_t input_message_buffer,
            gss_buffer_t output_message_buffer,
            int * conf_state,
            gss_qop_t * qop_state
           )
{
    ntlm_ctx ctx = (ntlm_ctx)context_handle;
    OM_uint32 ret;

    *minor_status = 0;
    output_message_buffer->value = NULL;
    output_message_buffer->length = 0;

    if (conf_state)
	*conf_state = 0;
    if (qop_state)
	*qop_state = 0;

    if (CTX_FLAGS_ISSET(ctx, NTLM_NEG_SEAL|NTLM_NEG_NTLM2_SESSION)) {

	return v2_unseal_message(input_message_buffer,
				 ctx->u.v2.recv.signkey,
				 ctx->u.v2.recv.seq++,
				 &ctx->u.v2.recv.sealkey,
				 output_message_buffer);

    } else if (CTX_FLAGS_ISSET(ctx, NTLM_NEG_SEAL)) {

	gss_buffer_desc trailer;
	OM_uint32 junk;

	if (input_message_buffer->length < 16)
	    return GSS_S_BAD_MIC;

	output_message_buffer->length = input_message_buffer->length - 16;
	output_message_buffer->value = malloc(output_message_buffer->length);
	if (output_message_buffer->value == NULL) {
	    output_message_buffer->length = 0;
	    return GSS_S_FAILURE;
	}

	RC4(&ctx->u.v1.crypto_recv.key, output_message_buffer->length,
	    input_message_buffer->value, output_message_buffer->value);

	trailer.value = ((unsigned char *)input_message_buffer->value) +
	    output_message_buffer->length;
	trailer.length = 16;

	ret = _gss_ntlm_verify_mic(minor_status, context_handle,
				   output_message_buffer,
				   &trailer, NULL);
	if (ret) {
	    gss_release_buffer(&junk, output_message_buffer);
	    return ret;
	}

	return GSS_S_COMPLETE;
    }

    return GSS_S_UNAVAILABLE;
}
