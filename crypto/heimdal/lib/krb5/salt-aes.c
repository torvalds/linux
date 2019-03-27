/*
 * Copyright (c) 1997 - 2008 Kungliga Tekniska Högskolan
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

int _krb5_AES_string_to_default_iterator = 4096;

static krb5_error_code
AES_string_to_key(krb5_context context,
		  krb5_enctype enctype,
		  krb5_data password,
		  krb5_salt salt,
		  krb5_data opaque,
		  krb5_keyblock *key)
{
    krb5_error_code ret;
    uint32_t iter;
    struct _krb5_encryption_type *et;
    struct _krb5_key_data kd;

    if (opaque.length == 0)
	iter = _krb5_AES_string_to_default_iterator;
    else if (opaque.length == 4) {
	unsigned long v;
	_krb5_get_int(opaque.data, &v, 4);
	iter = ((uint32_t)v);
    } else
	return KRB5_PROG_KEYTYPE_NOSUPP; /* XXX */

    et = _krb5_find_enctype(enctype);
    if (et == NULL)
	return KRB5_PROG_KEYTYPE_NOSUPP;

    kd.schedule = NULL;
    ALLOC(kd.key, 1);
    if(kd.key == NULL) {
	krb5_set_error_message (context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    kd.key->keytype = enctype;
    ret = krb5_data_alloc(&kd.key->keyvalue, et->keytype->size);
    if (ret) {
	krb5_set_error_message (context, ret, N_("malloc: out of memory", ""));
	return ret;
    }

    ret = PKCS5_PBKDF2_HMAC_SHA1(password.data, password.length,
				 salt.saltvalue.data, salt.saltvalue.length,
				 iter,
				 et->keytype->size, kd.key->keyvalue.data);
    if (ret != 1) {
	_krb5_free_key_data(context, &kd, et);
	krb5_set_error_message(context, KRB5_PROG_KEYTYPE_NOSUPP,
			       "Error calculating s2k");
	return KRB5_PROG_KEYTYPE_NOSUPP;
    }

    ret = _krb5_derive_key(context, et, &kd, "kerberos", strlen("kerberos"));
    if (ret == 0)
	ret = krb5_copy_keyblock_contents(context, kd.key, key);
    _krb5_free_key_data(context, &kd, et);

    return ret;
}

struct salt_type _krb5_AES_salt[] = {
    {
	KRB5_PW_SALT,
	"pw-salt",
	AES_string_to_key
    },
    { 0 }
};
