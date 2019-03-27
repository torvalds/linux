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

/*
 *
 */

static void
DES3_random_key(krb5_context context,
		krb5_keyblock *key)
{
    DES_cblock *k = key->keyvalue.data;
    do {
	krb5_generate_random_block(k, 3 * sizeof(DES_cblock));
	DES_set_odd_parity(&k[0]);
	DES_set_odd_parity(&k[1]);
	DES_set_odd_parity(&k[2]);
    } while(DES_is_weak_key(&k[0]) ||
	    DES_is_weak_key(&k[1]) ||
	    DES_is_weak_key(&k[2]));
}


#ifdef DES3_OLD_ENCTYPE
static struct _krb5_key_type keytype_des3 = {
    ETYPE_OLD_DES3_CBC_SHA1,
    "des3",
    168,
    24,
    sizeof(struct _krb5_evp_schedule),
    DES3_random_key,
    _krb5_evp_schedule,
    _krb5_des3_salt,
    _krb5_DES3_random_to_key,
    _krb5_evp_cleanup,
    EVP_des_ede3_cbc
};
#endif

static struct _krb5_key_type keytype_des3_derived = {
    ETYPE_OLD_DES3_CBC_SHA1,
    "des3",
    168,
    24,
    sizeof(struct _krb5_evp_schedule),
    DES3_random_key,
    _krb5_evp_schedule,
    _krb5_des3_salt_derived,
    _krb5_DES3_random_to_key,
    _krb5_evp_cleanup,
    EVP_des_ede3_cbc
};

#ifdef DES3_OLD_ENCTYPE
static krb5_error_code
RSA_MD5_DES3_checksum(krb5_context context,
		      struct _krb5_key_data *key,
		      const void *data,
		      size_t len,
		      unsigned usage,
		      Checksum *C)
{
    return _krb5_des_checksum(context, EVP_md5(), key, data, len, C);
}

static krb5_error_code
RSA_MD5_DES3_verify(krb5_context context,
		    struct _krb5_key_data *key,
		    const void *data,
		    size_t len,
		    unsigned usage,
		    Checksum *C)
{
    return _krb5_des_verify(context, EVP_md5(), key, data, len, C);
}

struct _krb5_checksum_type _krb5_checksum_rsa_md5_des3 = {
    CKSUMTYPE_RSA_MD5_DES3,
    "rsa-md5-des3",
    64,
    24,
    F_KEYED | F_CPROOF | F_VARIANT,
    RSA_MD5_DES3_checksum,
    RSA_MD5_DES3_verify
};
#endif

struct _krb5_checksum_type _krb5_checksum_hmac_sha1_des3 = {
    CKSUMTYPE_HMAC_SHA1_DES3,
    "hmac-sha1-des3",
    64,
    20,
    F_KEYED | F_CPROOF | F_DERIVED,
    _krb5_SP_HMAC_SHA1_checksum,
    NULL
};

#ifdef DES3_OLD_ENCTYPE
struct _krb5_encryption_type _krb5_enctype_des3_cbc_md5 = {
    ETYPE_DES3_CBC_MD5,
    "des3-cbc-md5",
    8,
    8,
    8,
    &keytype_des3,
    &_krb5_checksum_rsa_md5,
    &_krb5_checksum_rsa_md5_des3,
    0,
    _krb5_evp_encrypt,
    0,
    NULL
};
#endif

struct _krb5_encryption_type _krb5_enctype_des3_cbc_sha1 = {
    ETYPE_DES3_CBC_SHA1,
    "des3-cbc-sha1",
    8,
    8,
    8,
    &keytype_des3_derived,
    &_krb5_checksum_sha1,
    &_krb5_checksum_hmac_sha1_des3,
    F_DERIVED,
    _krb5_evp_encrypt,
    0,
    NULL
};

#ifdef DES3_OLD_ENCTYPE
struct _krb5_encryption_type _krb5_enctype_old_des3_cbc_sha1 = {
    ETYPE_OLD_DES3_CBC_SHA1,
    "old-des3-cbc-sha1",
    8,
    8,
    8,
    &keytype_des3,
    &_krb5_checksum_sha1,
    &_krb5_checksum_hmac_sha1_des3,
    0,
    _krb5_evp_encrypt,
    0,
    NULL
};
#endif

struct _krb5_encryption_type _krb5_enctype_des3_cbc_none = {
    ETYPE_DES3_CBC_NONE,
    "des3-cbc-none",
    8,
    8,
    0,
    &keytype_des3_derived,
    &_krb5_checksum_none,
    NULL,
    F_PSEUDO,
    _krb5_evp_encrypt,
    0,
    NULL
};

void
_krb5_DES3_random_to_key(krb5_context context,
			 krb5_keyblock *key,
			 const void *data,
			 size_t size)
{
    unsigned char *x = key->keyvalue.data;
    const u_char *q = data;
    DES_cblock *k;
    int i, j;

    memset(key->keyvalue.data, 0, key->keyvalue.length);
    for (i = 0; i < 3; ++i) {
	unsigned char foo;
	for (j = 0; j < 7; ++j) {
	    unsigned char b = q[7 * i + j];

	    x[8 * i + j] = b;
	}
	foo = 0;
	for (j = 6; j >= 0; --j) {
	    foo |= q[7 * i + j] & 1;
	    foo <<= 1;
	}
	x[8 * i + 7] = foo;
    }
    k = key->keyvalue.data;
    for (i = 0; i < 3; i++) {
	DES_set_odd_parity(&k[i]);
	if(DES_is_weak_key(&k[i]))
	    _krb5_xor(&k[i], (const unsigned char*)"\0\0\0\0\0\0\0\xf0");
    }
}
