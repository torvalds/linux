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

#ifndef HEIMDAL_SMALLER
#define DES3_OLD_ENCTYPE 1
#endif

struct _krb5_checksum_type *_krb5_checksum_types[] = {
    &_krb5_checksum_none,
#ifdef HEIM_WEAK_CRYPTO
    &_krb5_checksum_crc32,
    &_krb5_checksum_rsa_md4,
    &_krb5_checksum_rsa_md4_des,
    &_krb5_checksum_rsa_md5_des,
#endif
#ifdef DES3_OLD_ENCTYPE
    &_krb5_checksum_rsa_md5_des3,
#endif
    &_krb5_checksum_rsa_md5,
    &_krb5_checksum_sha1,
    &_krb5_checksum_hmac_sha1_des3,
    &_krb5_checksum_hmac_sha1_aes128,
    &_krb5_checksum_hmac_sha1_aes256,
    &_krb5_checksum_hmac_md5
};

int _krb5_num_checksums
	= sizeof(_krb5_checksum_types) / sizeof(_krb5_checksum_types[0]);

/*
 * these should currently be in reverse preference order.
 * (only relevant for !F_PSEUDO) */

struct _krb5_encryption_type *_krb5_etypes[] = {
    &_krb5_enctype_aes256_cts_hmac_sha1,
    &_krb5_enctype_aes128_cts_hmac_sha1,
    &_krb5_enctype_des3_cbc_sha1,
    &_krb5_enctype_des3_cbc_none, /* used by the gss-api mech */
    &_krb5_enctype_arcfour_hmac_md5,
#ifdef DES3_OLD_ENCTYPE
    &_krb5_enctype_des3_cbc_md5,
    &_krb5_enctype_old_des3_cbc_sha1,
#endif
#ifdef HEIM_WEAK_CRYPTO
    &_krb5_enctype_des_cbc_md5,
    &_krb5_enctype_des_cbc_md4,
    &_krb5_enctype_des_cbc_crc,
    &_krb5_enctype_des_cbc_none,
    &_krb5_enctype_des_cfb64_none,
    &_krb5_enctype_des_pcbc_none,
#endif
    &_krb5_enctype_null
};

int _krb5_num_etypes = sizeof(_krb5_etypes) / sizeof(_krb5_etypes[0]);
