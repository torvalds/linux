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

krb5_error_code
_gsskrb5_encode_om_uint32(OM_uint32 n, u_char *p)
{
  p[0] = (n >> 0)  & 0xFF;
  p[1] = (n >> 8)  & 0xFF;
  p[2] = (n >> 16) & 0xFF;
  p[3] = (n >> 24) & 0xFF;
  return 0;
}

krb5_error_code
_gsskrb5_encode_be_om_uint32(OM_uint32 n, u_char *p)
{
  p[0] = (n >> 24) & 0xFF;
  p[1] = (n >> 16) & 0xFF;
  p[2] = (n >> 8)  & 0xFF;
  p[3] = (n >> 0)  & 0xFF;
  return 0;
}

krb5_error_code
_gsskrb5_decode_om_uint32(const void *ptr, OM_uint32 *n)
{
    const u_char *p = ptr;
    *n = (p[0] << 0) | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    return 0;
}

krb5_error_code
_gsskrb5_decode_be_om_uint32(const void *ptr, OM_uint32 *n)
{
    const u_char *p = ptr;
    *n = (p[0] <<24) | (p[1] << 16) | (p[2] << 8) | (p[3] << 0);
    return 0;
}

static krb5_error_code
hash_input_chan_bindings (const gss_channel_bindings_t b,
			  u_char *p)
{
  u_char num[4];
  EVP_MD_CTX *ctx;

  ctx = EVP_MD_CTX_create();
  EVP_DigestInit_ex(ctx, EVP_md5(), NULL);

  _gsskrb5_encode_om_uint32 (b->initiator_addrtype, num);
  EVP_DigestUpdate(ctx, num, sizeof(num));
  _gsskrb5_encode_om_uint32 (b->initiator_address.length, num);
  EVP_DigestUpdate(ctx, num, sizeof(num));
  if (b->initiator_address.length)
      EVP_DigestUpdate(ctx,
		       b->initiator_address.value,
		       b->initiator_address.length);
  _gsskrb5_encode_om_uint32 (b->acceptor_addrtype, num);
  EVP_DigestUpdate(ctx, num, sizeof(num));
  _gsskrb5_encode_om_uint32 (b->acceptor_address.length, num);
  EVP_DigestUpdate(ctx, num, sizeof(num));
  if (b->acceptor_address.length)
      EVP_DigestUpdate(ctx,
		       b->acceptor_address.value,
		       b->acceptor_address.length);
  _gsskrb5_encode_om_uint32 (b->application_data.length, num);
  EVP_DigestUpdate(ctx, num, sizeof(num));
  if (b->application_data.length)
      EVP_DigestUpdate(ctx,
		       b->application_data.value,
		       b->application_data.length);
  EVP_DigestFinal_ex(ctx, p, NULL);
  EVP_MD_CTX_destroy(ctx);

  return 0;
}

/*
 * create a checksum over the chanel bindings in
 * `input_chan_bindings', `flags' and `fwd_data' and return it in
 * `result'
 */

OM_uint32
_gsskrb5_create_8003_checksum (
		      OM_uint32 *minor_status,
		      const gss_channel_bindings_t input_chan_bindings,
		      OM_uint32 flags,
		      const krb5_data *fwd_data,
		      Checksum *result)
{
    u_char *p;

    /*
     * see rfc1964 (section 1.1.1 (Initial Token), and the checksum value
     * field's format) */
    result->cksumtype = CKSUMTYPE_GSSAPI;
    if (fwd_data->length > 0 && (flags & GSS_C_DELEG_FLAG))
	result->checksum.length = 24 + 4 + fwd_data->length;
    else
	result->checksum.length = 24;
    result->checksum.data   = malloc (result->checksum.length);
    if (result->checksum.data == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    p = result->checksum.data;
    _gsskrb5_encode_om_uint32 (16, p);
    p += 4;
    if (input_chan_bindings == GSS_C_NO_CHANNEL_BINDINGS) {
	memset (p, 0, 16);
    } else {
	hash_input_chan_bindings (input_chan_bindings, p);
    }
    p += 16;
    _gsskrb5_encode_om_uint32 (flags, p);
    p += 4;

    if (fwd_data->length > 0 && (flags & GSS_C_DELEG_FLAG)) {

	*p++ = (1 >> 0) & 0xFF;                   /* DlgOpt */ /* == 1 */
	*p++ = (1 >> 8) & 0xFF;                   /* DlgOpt */ /* == 0 */
	*p++ = (fwd_data->length >> 0) & 0xFF;    /* Dlgth  */
	*p++ = (fwd_data->length >> 8) & 0xFF;    /* Dlgth  */
	memcpy(p, (unsigned char *) fwd_data->data, fwd_data->length);

	p += fwd_data->length;
    }

    return GSS_S_COMPLETE;
}

/*
 * verify the checksum in `cksum' over `input_chan_bindings'
 * returning  `flags' and `fwd_data'
 */

OM_uint32
_gsskrb5_verify_8003_checksum(
		      OM_uint32 *minor_status,
		      const gss_channel_bindings_t input_chan_bindings,
		      const Checksum *cksum,
		      OM_uint32 *flags,
		      krb5_data *fwd_data)
{
    unsigned char hash[16];
    unsigned char *p;
    OM_uint32 length;
    int DlgOpt;
    static unsigned char zeros[16];

    /* XXX should handle checksums > 24 bytes */
    if(cksum->cksumtype != CKSUMTYPE_GSSAPI || cksum->checksum.length < 24) {
	*minor_status = 0;
	return GSS_S_BAD_BINDINGS;
    }

    p = cksum->checksum.data;
    _gsskrb5_decode_om_uint32(p, &length);
    if(length != sizeof(hash)) {
	*minor_status = 0;
	return GSS_S_BAD_BINDINGS;
    }

    p += 4;

    if (input_chan_bindings != GSS_C_NO_CHANNEL_BINDINGS
	&& memcmp(p, zeros, sizeof(zeros)) != 0) {
	if(hash_input_chan_bindings(input_chan_bindings, hash) != 0) {
	    *minor_status = 0;
	    return GSS_S_BAD_BINDINGS;
	}
	if(ct_memcmp(hash, p, sizeof(hash)) != 0) {
	    *minor_status = 0;
	    return GSS_S_BAD_BINDINGS;
	}
    }

    p += sizeof(hash);

    _gsskrb5_decode_om_uint32(p, flags);
    p += 4;

    if (cksum->checksum.length > 24 && (*flags & GSS_C_DELEG_FLAG)) {
	if(cksum->checksum.length < 28) {
	    *minor_status = 0;
	    return GSS_S_BAD_BINDINGS;
	}

	DlgOpt = (p[0] << 0) | (p[1] << 8);
	p += 2;
	if (DlgOpt != 1) {
	    *minor_status = 0;
	    return GSS_S_BAD_BINDINGS;
	}

	fwd_data->length = (p[0] << 0) | (p[1] << 8);
	p += 2;
	if(cksum->checksum.length < 28 + fwd_data->length) {
	    *minor_status = 0;
	    return GSS_S_BAD_BINDINGS;
	}
	fwd_data->data = malloc(fwd_data->length);
	if (fwd_data->data == NULL) {
	    *minor_status = ENOMEM;
	    return GSS_S_FAILURE;
	}
	memcpy(fwd_data->data, p, fwd_data->length);
    }

    return GSS_S_COMPLETE;
}
