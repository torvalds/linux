/*
 * Copyright (c) 1997 - 2004 Kungliga Tekniska Högskolan
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

#ifdef HEIM_WEAK_CRYPTO

static OM_uint32
unwrap_des
           (OM_uint32 * minor_status,
            const gsskrb5_ctx context_handle,
            const gss_buffer_t input_message_buffer,
            gss_buffer_t output_message_buffer,
            int * conf_state,
            gss_qop_t * qop_state,
	    krb5_keyblock *key
           )
{
  u_char *p, *seq;
  size_t len;
  EVP_MD_CTX *md5;
  u_char hash[16];
  EVP_CIPHER_CTX *des_ctx;
  DES_key_schedule schedule;
  DES_cblock deskey;
  DES_cblock zero;
  size_t i;
  uint32_t seq_number;
  size_t padlength;
  OM_uint32 ret;
  int cstate;
  int cmp;
  int token_len;

  if (IS_DCE_STYLE(context_handle)) {
     token_len = 22 + 8 + 15; /* 45 */
  } else {
     token_len = input_message_buffer->length;
  }

  p = input_message_buffer->value;
  ret = _gsskrb5_verify_header (&p,
				   token_len,
				   "\x02\x01",
				   GSS_KRB5_MECHANISM);
  if (ret)
      return ret;

  if (memcmp (p, "\x00\x00", 2) != 0)
    return GSS_S_BAD_SIG;
  p += 2;
  if (memcmp (p, "\x00\x00", 2) == 0) {
      cstate = 1;
  } else if (memcmp (p, "\xFF\xFF", 2) == 0) {
      cstate = 0;
  } else
      return GSS_S_BAD_MIC;
  p += 2;
  if(conf_state != NULL)
      *conf_state = cstate;
  if (memcmp (p, "\xff\xff", 2) != 0)
    return GSS_S_DEFECTIVE_TOKEN;
  p += 2;
  p += 16;

  len = p - (u_char *)input_message_buffer->value;

  if(cstate) {
      /* decrypt data */
      memcpy (&deskey, key->keyvalue.data, sizeof(deskey));
      memset (&zero, 0, sizeof(zero));

      for (i = 0; i < sizeof(deskey); ++i)
	  deskey[i] ^= 0xf0;


      des_ctx = EVP_CIPHER_CTX_new();
      if (des_ctx == NULL) {
	  memset (deskey, 0, sizeof(deskey));
	  *minor_status = ENOMEM;
	  return GSS_S_FAILURE;
      }
      EVP_CipherInit_ex(des_ctx, EVP_des_cbc(), NULL, deskey, zero, 0);
      EVP_Cipher(des_ctx, p, p, input_message_buffer->length - len);
      EVP_CIPHER_CTX_free(des_ctx);

      memset (deskey, 0, sizeof(deskey));
  }

  if (IS_DCE_STYLE(context_handle)) {
    padlength = 0;
  } else {
    /* check pad */
    ret = _gssapi_verify_pad(input_message_buffer,
			     input_message_buffer->length - len,
			     &padlength);
    if (ret)
        return ret;
  }

  md5 = EVP_MD_CTX_create();
  EVP_DigestInit_ex(md5, EVP_md5(), NULL);
  EVP_DigestUpdate(md5, p - 24, 8);
  EVP_DigestUpdate(md5, p, input_message_buffer->length - len);
  EVP_DigestFinal_ex(md5, hash, NULL);
  EVP_MD_CTX_destroy(md5);

  memset (&zero, 0, sizeof(zero));
  memcpy (&deskey, key->keyvalue.data, sizeof(deskey));
  DES_set_key_unchecked (&deskey, &schedule);
  DES_cbc_cksum ((void *)hash, (void *)hash, sizeof(hash),
		 &schedule, &zero);
  if (ct_memcmp (p - 8, hash, 8) != 0) {
    memset (deskey, 0, sizeof(deskey));
    memset (&schedule, 0, sizeof(schedule));
    return GSS_S_BAD_MIC;
  }

  /* verify sequence number */

  des_ctx = EVP_CIPHER_CTX_new();
  if (des_ctx == NULL) {
    memset (deskey, 0, sizeof(deskey));
    memset (&schedule, 0, sizeof(schedule));
    *minor_status = ENOMEM;
    return GSS_S_FAILURE;
  }

  HEIMDAL_MUTEX_lock(&context_handle->ctx_id_mutex);

  p -= 16;

  EVP_CipherInit_ex(des_ctx, EVP_des_cbc(), NULL, key->keyvalue.data, hash, 0);
  EVP_Cipher(des_ctx, p, p, 8);
  EVP_CIPHER_CTX_free(des_ctx);

  memset (deskey, 0, sizeof(deskey));
  memset (&schedule, 0, sizeof(schedule));

  seq = p;
  _gsskrb5_decode_om_uint32(seq, &seq_number);

  if (context_handle->more_flags & LOCAL)
      cmp = ct_memcmp(&seq[4], "\xff\xff\xff\xff", 4);
  else
      cmp = ct_memcmp(&seq[4], "\x00\x00\x00\x00", 4);

  if (cmp != 0) {
    HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);
    return GSS_S_BAD_MIC;
  }

  ret = _gssapi_msg_order_check(context_handle->order, seq_number);
  if (ret) {
    HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);
    return ret;
  }

  HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);

  /* copy out data */

  output_message_buffer->length = input_message_buffer->length
    - len - padlength - 8;
  output_message_buffer->value  = malloc(output_message_buffer->length);
  if(output_message_buffer->length != 0 && output_message_buffer->value == NULL)
      return GSS_S_FAILURE;
  memcpy (output_message_buffer->value,
	  p + 24,
	  output_message_buffer->length);
  return GSS_S_COMPLETE;
}
#endif

static OM_uint32
unwrap_des3
           (OM_uint32 * minor_status,
            const gsskrb5_ctx context_handle,
	    krb5_context context,
            const gss_buffer_t input_message_buffer,
            gss_buffer_t output_message_buffer,
            int * conf_state,
            gss_qop_t * qop_state,
	    krb5_keyblock *key
           )
{
  u_char *p;
  size_t len;
  u_char *seq;
  krb5_data seq_data;
  u_char cksum[20];
  uint32_t seq_number;
  size_t padlength;
  OM_uint32 ret;
  int cstate;
  krb5_crypto crypto;
  Checksum csum;
  int cmp;
  int token_len;

  if (IS_DCE_STYLE(context_handle)) {
     token_len = 34 + 8 + 15; /* 57 */
  } else {
     token_len = input_message_buffer->length;
  }

  p = input_message_buffer->value;
  ret = _gsskrb5_verify_header (&p,
				   token_len,
				   "\x02\x01",
				   GSS_KRB5_MECHANISM);
  if (ret)
      return ret;

  if (memcmp (p, "\x04\x00", 2) != 0) /* HMAC SHA1 DES3_KD */
    return GSS_S_BAD_SIG;
  p += 2;
  if (ct_memcmp (p, "\x02\x00", 2) == 0) {
    cstate = 1;
  } else if (ct_memcmp (p, "\xff\xff", 2) == 0) {
    cstate = 0;
  } else
    return GSS_S_BAD_MIC;
  p += 2;
  if(conf_state != NULL)
    *conf_state = cstate;
  if (ct_memcmp (p, "\xff\xff", 2) != 0)
    return GSS_S_DEFECTIVE_TOKEN;
  p += 2;
  p += 28;

  len = p - (u_char *)input_message_buffer->value;

  if(cstate) {
      /* decrypt data */
      krb5_data tmp;

      ret = krb5_crypto_init(context, key,
			     ETYPE_DES3_CBC_NONE, &crypto);
      if (ret) {
	  *minor_status = ret;
	  return GSS_S_FAILURE;
      }
      ret = krb5_decrypt(context, crypto, KRB5_KU_USAGE_SEAL,
			 p, input_message_buffer->length - len, &tmp);
      krb5_crypto_destroy(context, crypto);
      if (ret) {
	  *minor_status = ret;
	  return GSS_S_FAILURE;
      }
      assert (tmp.length == input_message_buffer->length - len);

      memcpy (p, tmp.data, tmp.length);
      krb5_data_free(&tmp);
  }

  if (IS_DCE_STYLE(context_handle)) {
    padlength = 0;
  } else {
    /* check pad */
    ret = _gssapi_verify_pad(input_message_buffer,
			     input_message_buffer->length - len,
			     &padlength);
    if (ret)
        return ret;
  }

  /* verify sequence number */

  HEIMDAL_MUTEX_lock(&context_handle->ctx_id_mutex);

  p -= 28;

  ret = krb5_crypto_init(context, key,
			 ETYPE_DES3_CBC_NONE, &crypto);
  if (ret) {
      *minor_status = ret;
      HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);
      return GSS_S_FAILURE;
  }
  {
      DES_cblock ivec;

      memcpy(&ivec, p + 8, 8);
      ret = krb5_decrypt_ivec (context,
			       crypto,
			       KRB5_KU_USAGE_SEQ,
			       p, 8, &seq_data,
			       &ivec);
  }
  krb5_crypto_destroy (context, crypto);
  if (ret) {
      *minor_status = ret;
      HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);
      return GSS_S_FAILURE;
  }
  if (seq_data.length != 8) {
      krb5_data_free (&seq_data);
      *minor_status = 0;
      HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);
      return GSS_S_BAD_MIC;
  }

  seq = seq_data.data;
  _gsskrb5_decode_om_uint32(seq, &seq_number);

  if (context_handle->more_flags & LOCAL)
      cmp = ct_memcmp(&seq[4], "\xff\xff\xff\xff", 4);
  else
      cmp = ct_memcmp(&seq[4], "\x00\x00\x00\x00", 4);

  krb5_data_free (&seq_data);
  if (cmp != 0) {
      *minor_status = 0;
      HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);
      return GSS_S_BAD_MIC;
  }

  ret = _gssapi_msg_order_check(context_handle->order, seq_number);
  if (ret) {
      *minor_status = 0;
      HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);
      return ret;
  }

  HEIMDAL_MUTEX_unlock(&context_handle->ctx_id_mutex);

  /* verify checksum */

  memcpy (cksum, p + 8, 20);

  memcpy (p + 20, p - 8, 8);

  csum.cksumtype = CKSUMTYPE_HMAC_SHA1_DES3;
  csum.checksum.length = 20;
  csum.checksum.data   = cksum;

  ret = krb5_crypto_init(context, key, 0, &crypto);
  if (ret) {
      *minor_status = ret;
      return GSS_S_FAILURE;
  }

  ret = krb5_verify_checksum (context, crypto,
			      KRB5_KU_USAGE_SIGN,
			      p + 20,
			      input_message_buffer->length - len + 8,
			      &csum);
  krb5_crypto_destroy (context, crypto);
  if (ret) {
      *minor_status = ret;
      return GSS_S_FAILURE;
  }

  /* copy out data */

  output_message_buffer->length = input_message_buffer->length
    - len - padlength - 8;
  output_message_buffer->value  = malloc(output_message_buffer->length);
  if(output_message_buffer->length != 0 && output_message_buffer->value == NULL)
      return GSS_S_FAILURE;
  memcpy (output_message_buffer->value,
	  p + 36,
	  output_message_buffer->length);
  return GSS_S_COMPLETE;
}

OM_uint32 GSSAPI_CALLCONV _gsskrb5_unwrap
           (OM_uint32 * minor_status,
            const gss_ctx_id_t context_handle,
            const gss_buffer_t input_message_buffer,
            gss_buffer_t output_message_buffer,
            int * conf_state,
            gss_qop_t * qop_state
           )
{
  krb5_keyblock *key;
  krb5_context context;
  OM_uint32 ret;
  krb5_keytype keytype;
  gsskrb5_ctx ctx = (gsskrb5_ctx) context_handle;

  output_message_buffer->value = NULL;
  output_message_buffer->length = 0;
  if (qop_state != NULL)
      *qop_state = GSS_C_QOP_DEFAULT;

  GSSAPI_KRB5_INIT (&context);

  if (ctx->more_flags & IS_CFX)
      return _gssapi_unwrap_cfx (minor_status, ctx, context,
				 input_message_buffer, output_message_buffer,
				 conf_state, qop_state);

  HEIMDAL_MUTEX_lock(&ctx->ctx_id_mutex);
  ret = _gsskrb5i_get_token_key(ctx, context, &key);
  HEIMDAL_MUTEX_unlock(&ctx->ctx_id_mutex);
  if (ret) {
      *minor_status = ret;
      return GSS_S_FAILURE;
  }
  krb5_enctype_to_keytype (context, key->keytype, &keytype);

  *minor_status = 0;

  switch (keytype) {
  case KEYTYPE_DES :
#ifdef HEIM_WEAK_CRYPTO
      ret = unwrap_des (minor_status, ctx,
			input_message_buffer, output_message_buffer,
			conf_state, qop_state, key);
#else
      ret = GSS_S_FAILURE;
#endif
      break;
  case KEYTYPE_DES3 :
      ret = unwrap_des3 (minor_status, ctx, context,
			 input_message_buffer, output_message_buffer,
			 conf_state, qop_state, key);
      break;
  case KEYTYPE_ARCFOUR:
  case KEYTYPE_ARCFOUR_56:
      ret = _gssapi_unwrap_arcfour (minor_status, ctx, context,
				    input_message_buffer, output_message_buffer,
				    conf_state, qop_state, key);
      break;
  default :
      abort();
      break;
  }
  krb5_free_keyblock (context, key);
  return ret;
}
