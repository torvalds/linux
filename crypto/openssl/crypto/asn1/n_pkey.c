/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "openssl/opensslconf.h"
#ifdef OPENSSL_NO_RSA
NON_EMPTY_TRANSLATION_UNIT
#else

# include "internal/cryptlib.h"
# include <stdio.h>
# include <openssl/rsa.h>
# include <openssl/objects.h>
# include <openssl/asn1t.h>
# include <openssl/evp.h>
# include <openssl/x509.h>

# ifndef OPENSSL_NO_RC4

typedef struct netscape_pkey_st {
    int32_t version;
    X509_ALGOR *algor;
    ASN1_OCTET_STRING *private_key;
} NETSCAPE_PKEY;

typedef struct netscape_encrypted_pkey_st {
    ASN1_OCTET_STRING *os;
    /*
     * This is the same structure as DigestInfo so use it: although this
     * isn't really anything to do with digests.
     */
    X509_SIG *enckey;
} NETSCAPE_ENCRYPTED_PKEY;


ASN1_BROKEN_SEQUENCE(NETSCAPE_ENCRYPTED_PKEY) = {
        ASN1_SIMPLE(NETSCAPE_ENCRYPTED_PKEY, os, ASN1_OCTET_STRING),
        ASN1_SIMPLE(NETSCAPE_ENCRYPTED_PKEY, enckey, X509_SIG)
} static_ASN1_BROKEN_SEQUENCE_END(NETSCAPE_ENCRYPTED_PKEY)

DECLARE_ASN1_FUNCTIONS_const(NETSCAPE_ENCRYPTED_PKEY)
DECLARE_ASN1_ENCODE_FUNCTIONS_const(NETSCAPE_ENCRYPTED_PKEY,NETSCAPE_ENCRYPTED_PKEY)
IMPLEMENT_ASN1_FUNCTIONS_const(NETSCAPE_ENCRYPTED_PKEY)

ASN1_SEQUENCE(NETSCAPE_PKEY) = {
        ASN1_EMBED(NETSCAPE_PKEY, version, INT32),
        ASN1_SIMPLE(NETSCAPE_PKEY, algor, X509_ALGOR),
        ASN1_SIMPLE(NETSCAPE_PKEY, private_key, ASN1_OCTET_STRING)
} static_ASN1_SEQUENCE_END(NETSCAPE_PKEY)

DECLARE_ASN1_FUNCTIONS_const(NETSCAPE_PKEY)
DECLARE_ASN1_ENCODE_FUNCTIONS_const(NETSCAPE_PKEY,NETSCAPE_PKEY)
IMPLEMENT_ASN1_FUNCTIONS_const(NETSCAPE_PKEY)

# endif                         /* OPENSSL_NO_RC4 */

#endif
