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

#include "krb5_locl.h"

/**
 * Zero out a keyblock
 *
 * @param keyblock keyblock to zero out
 *
 * @ingroup krb5_crypto
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_keyblock_zero(krb5_keyblock *keyblock)
{
    keyblock->keytype = 0;
    krb5_data_zero(&keyblock->keyvalue);
}

/**
 * Free a keyblock's content, also zero out the content of the keyblock.
 *
 * @param context a Kerberos 5 context
 * @param keyblock keyblock content to free, NULL is valid argument
 *
 * @ingroup krb5_crypto
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_keyblock_contents(krb5_context context,
			    krb5_keyblock *keyblock)
{
    if(keyblock) {
	if (keyblock->keyvalue.data != NULL)
	    memset(keyblock->keyvalue.data, 0, keyblock->keyvalue.length);
	krb5_data_free (&keyblock->keyvalue);
	keyblock->keytype = ENCTYPE_NULL;
    }
}

/**
 * Free a keyblock, also zero out the content of the keyblock, uses
 * krb5_free_keyblock_contents() to free the content.
 *
 * @param context a Kerberos 5 context
 * @param keyblock keyblock to free, NULL is valid argument
 *
 * @ingroup krb5_crypto
 */

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_keyblock(krb5_context context,
		   krb5_keyblock *keyblock)
{
    if(keyblock){
	krb5_free_keyblock_contents(context, keyblock);
	free(keyblock);
    }
}

/**
 * Copy a keyblock, free the output keyblock with
 * krb5_free_keyblock_contents().
 *
 * @param context a Kerberos 5 context
 * @param inblock the key to copy
 * @param to the output key.
 *
 * @return 0 on success or a Kerberos 5 error code
 *
 * @ingroup krb5_crypto
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_keyblock_contents (krb5_context context,
			     const krb5_keyblock *inblock,
			     krb5_keyblock *to)
{
    return copy_EncryptionKey(inblock, to);
}

/**
 * Copy a keyblock, free the output keyblock with
 * krb5_free_keyblock().
 *
 * @param context a Kerberos 5 context
 * @param inblock the key to copy
 * @param to the output key.
 *
 * @return 0 on success or a Kerberos 5 error code
 *
 * @ingroup krb5_crypto
 */


KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_keyblock (krb5_context context,
		    const krb5_keyblock *inblock,
		    krb5_keyblock **to)
{
    krb5_error_code ret;
    krb5_keyblock *k;

    *to = NULL;

    k = calloc (1, sizeof(*k));
    if (k == NULL) {
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }

    ret = krb5_copy_keyblock_contents (context, inblock, k);
    if (ret) {
      free(k);
      return ret;
    }
    *to = k;
    return 0;
}

/**
 * Get encryption type of a keyblock.
 *
 * @ingroup krb5_crypto
 */

KRB5_LIB_FUNCTION krb5_enctype KRB5_LIB_CALL
krb5_keyblock_get_enctype(const krb5_keyblock *block)
{
    return block->keytype;
}

/**
 * Fill in `key' with key data of type `enctype' from `data' of length
 * `size'. Key should be freed using krb5_free_keyblock_contents().
 *
 * @return 0 on success or a Kerberos 5 error code
 *
 * @ingroup krb5_crypto
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_keyblock_init(krb5_context context,
		   krb5_enctype type,
		   const void *data,
		   size_t size,
		   krb5_keyblock *key)
{
    krb5_error_code ret;
    size_t len;

    memset(key, 0, sizeof(*key));

    ret = krb5_enctype_keysize(context, type, &len);
    if (ret)
	return ret;

    if (len != size) {
	krb5_set_error_message(context, KRB5_PROG_ETYPE_NOSUPP,
			       "Encryption key %d is %lu bytes "
			       "long, %lu was passed in",
			       type, (unsigned long)len, (unsigned long)size);
	return KRB5_PROG_ETYPE_NOSUPP;
    }
    ret = krb5_data_copy(&key->keyvalue, data, len);
    if(ret) {
	krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	return ret;
    }
    key->keytype = type;

    return 0;
}
