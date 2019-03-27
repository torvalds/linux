/*
 * Copyright (c) 2003 Kungliga Tekniska Högskolan
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

#include "krb5_locl.h"

#ifndef HEIMDAL_SMALLER

/*
 * Glue for MIT API
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_make_checksum(krb5_context context,
		     krb5_cksumtype cksumtype,
		     const krb5_keyblock *key,
		     krb5_keyusage usage,
		     const krb5_data *input,
		     krb5_checksum *cksum)
{
    krb5_error_code ret;
    krb5_crypto crypto;

    ret = krb5_crypto_init(context, key, 0, &crypto);
    if (ret)
	return ret;

    ret = krb5_create_checksum(context, crypto,  usage, cksumtype,
			       input->data, input->length, cksum);
    krb5_crypto_destroy(context, crypto);

    return ret ;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_verify_checksum(krb5_context context, const krb5_keyblock *key,
		       krb5_keyusage usage, const krb5_data *data,
		       const krb5_checksum *cksum, krb5_boolean *valid)
{
    krb5_error_code ret;
    krb5_checksum data_cksum;

    *valid = 0;

    ret = krb5_c_make_checksum(context, cksum->cksumtype,
			       key, usage, data, &data_cksum);
    if (ret)
	return ret;

    if (data_cksum.cksumtype == cksum->cksumtype
	&& krb5_data_ct_cmp(&data_cksum.checksum, &cksum->checksum) == 0)
	*valid = 1;

    krb5_free_checksum_contents(context, &data_cksum);

    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_get_checksum(krb5_context context, const krb5_checksum *cksum,
		    krb5_cksumtype *type, krb5_data **data)
{
    krb5_error_code ret;

    if (type)
	*type = cksum->cksumtype;
    if (data) {
	*data = malloc(sizeof(**data));
	if (*data == NULL)
	    return ENOMEM;

	ret = der_copy_octet_string(&cksum->checksum, *data);
	if (ret) {
	    free(*data);
	    *data = NULL;
	    return ret;
	}
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_set_checksum(krb5_context context, krb5_checksum *cksum,
		    krb5_cksumtype type, const krb5_data *data)
{
    cksum->cksumtype = type;
    return der_copy_octet_string(data, &cksum->checksum);
}

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_checksum (krb5_context context, krb5_checksum *cksum)
{
    krb5_checksum_free(context, cksum);
    free(cksum);
}

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_checksum_contents(krb5_context context, krb5_checksum *cksum)
{
    krb5_checksum_free(context, cksum);
    memset(cksum, 0, sizeof(*cksum));
}

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_checksum_free(krb5_context context, krb5_checksum *cksum)
{
    free_Checksum(cksum);
}

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_c_valid_enctype (krb5_enctype etype)
{
    return !krb5_enctype_valid(NULL, etype);
}

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_c_valid_cksumtype(krb5_cksumtype ctype)
{
    return krb5_cksumtype_valid(NULL, ctype);
}

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_c_is_coll_proof_cksum(krb5_cksumtype ctype)
{
    return krb5_checksum_is_collision_proof(NULL, ctype);
}

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_c_is_keyed_cksum(krb5_cksumtype ctype)
{
    return krb5_checksum_is_keyed(NULL, ctype);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_checksum (krb5_context context,
		    const krb5_checksum *old,
		    krb5_checksum **new)
{
    *new = malloc(sizeof(**new));
    if (*new == NULL)
	return ENOMEM;
    return copy_Checksum(old, *new);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_checksum_length (krb5_context context, krb5_cksumtype cksumtype,
			size_t *length)
{
    return krb5_checksumsize(context, cksumtype, length);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_block_size(krb5_context context,
		  krb5_enctype enctype,
		  size_t *blocksize)
{
    krb5_error_code ret;
    krb5_crypto crypto;
    krb5_keyblock key;

    ret = krb5_generate_random_keyblock(context, enctype, &key);
    if (ret)
	return ret;

    ret = krb5_crypto_init(context, &key, 0, &crypto);
    krb5_free_keyblock_contents(context, &key);
    if (ret)
	return ret;
    ret = krb5_crypto_getblocksize(context, crypto, blocksize);
    krb5_crypto_destroy(context, crypto);

    return ret;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_decrypt(krb5_context context,
	       const krb5_keyblock key,
	       krb5_keyusage usage,
	       const krb5_data *ivec,
	       krb5_enc_data *input,
	       krb5_data *output)
{
    krb5_error_code ret;
    krb5_crypto crypto;

    ret = krb5_crypto_init(context, &key, input->enctype, &crypto);
    if (ret)
	return ret;

    if (ivec) {
	size_t blocksize;

	ret = krb5_crypto_getblocksize(context, crypto, &blocksize);
	if (ret) {
	krb5_crypto_destroy(context, crypto);
	return ret;
	}

	if (blocksize > ivec->length) {
	    krb5_crypto_destroy(context, crypto);
	    return KRB5_BAD_MSIZE;
	}
    }

    ret = krb5_decrypt_ivec(context, crypto, usage,
			    input->ciphertext.data, input->ciphertext.length,
			    output,
			    ivec ? ivec->data : NULL);

    krb5_crypto_destroy(context, crypto);

    return ret ;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_encrypt(krb5_context context,
	       const krb5_keyblock *key,
	       krb5_keyusage usage,
	       const krb5_data *ivec,
	       const krb5_data *input,
	       krb5_enc_data *output)
{
    krb5_error_code ret;
    krb5_crypto crypto;

    ret = krb5_crypto_init(context, key, 0, &crypto);
    if (ret)
	return ret;

    if (ivec) {
	size_t blocksize;

	ret = krb5_crypto_getblocksize(context, crypto, &blocksize);
	if (ret) {
	    krb5_crypto_destroy(context, crypto);
	    return ret;
	}

	if (blocksize > ivec->length) {
	    krb5_crypto_destroy(context, crypto);
	    return KRB5_BAD_MSIZE;
	}
    }

    ret = krb5_encrypt_ivec(context, crypto, usage,
			    input->data, input->length,
			    &output->ciphertext,
			    ivec ? ivec->data : NULL);
    output->kvno = 0;
    krb5_crypto_getenctype(context, crypto, &output->enctype);

    krb5_crypto_destroy(context, crypto);

    return ret ;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_encrypt_length(krb5_context context,
		      krb5_enctype enctype,
		      size_t inputlen,
		      size_t *length)
{
    krb5_error_code ret;
    krb5_crypto crypto;
    krb5_keyblock key;

    ret = krb5_generate_random_keyblock(context, enctype, &key);
    if (ret)
	return ret;

    ret = krb5_crypto_init(context, &key, 0, &crypto);
    krb5_free_keyblock_contents(context, &key);
    if (ret)
	return ret;

    *length = krb5_get_wrapped_length(context, crypto, inputlen);
    krb5_crypto_destroy(context, crypto);

    return 0;
}

/**
 * Deprecated: keytypes doesn't exists, they are really enctypes.
 *
 * @ingroup krb5_deprecated
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_enctype_compare(krb5_context context,
		       krb5_enctype e1,
		       krb5_enctype e2,
		       krb5_boolean *similar)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    *similar = (e1 == e2);
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_make_random_key(krb5_context context,
		       krb5_enctype enctype,
		       krb5_keyblock *random_key)
{
    return krb5_generate_random_keyblock(context, enctype, random_key);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_keylengths(krb5_context context,
		  krb5_enctype enctype,
		  size_t *ilen,
		  size_t *keylen)
{
    krb5_error_code ret;

    ret = krb5_enctype_keybits(context, enctype, ilen);
    if (ret)
	return ret;
    *ilen = (*ilen + 7) / 8;
    return krb5_enctype_keysize(context, enctype, keylen);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_prf_length(krb5_context context,
		  krb5_enctype type,
		  size_t *length)
{
    return krb5_crypto_prf_length(context, type, length);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_prf(krb5_context context,
	   const krb5_keyblock *key,
	   const krb5_data *input,
	   krb5_data *output)
{
    krb5_crypto crypto;
    krb5_error_code ret;

    ret = krb5_crypto_init(context, key, 0, &crypto);
    if (ret)
	return ret;

    ret = krb5_crypto_prf(context, crypto, input, output);
    krb5_crypto_destroy(context, crypto);

    return ret;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_c_random_make_octets(krb5_context context, krb5_data * data)
{
    return krb5_generate_random_keyblock(context, data->length, data->data);
}

/**
 * MIT compat glue
 *
 * @ingroup krb5_ccache
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_cc_copy_creds(krb5_context context,
		   const krb5_ccache from,
		   krb5_ccache to)
{
    return krb5_cc_copy_cache(context, from, to);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getsendsubkey(krb5_context context, krb5_auth_context auth_context,
                            krb5_keyblock **keyblock)
{
    return krb5_auth_con_getlocalsubkey(context, auth_context, keyblock);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_getrecvsubkey(krb5_context context, krb5_auth_context auth_context,
                            krb5_keyblock **keyblock)
{
    return krb5_auth_con_getremotesubkey(context, auth_context, keyblock);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setsendsubkey(krb5_context context, krb5_auth_context auth_context,
                            krb5_keyblock *keyblock)
{
    return krb5_auth_con_setlocalsubkey(context, auth_context, keyblock);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_auth_con_setrecvsubkey(krb5_context context, krb5_auth_context auth_context,
                            krb5_keyblock *keyblock)
{
    return krb5_auth_con_setremotesubkey(context, auth_context, keyblock);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_default_realm(krb5_context context, krb5_realm realm)
{
    return krb5_xfree(realm);
}

#endif /* HEIMDAL_SMALLER */
