/*
 * Copyright 2006-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/objects.h>
#include <openssl/bn.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/ts.h>
#include "ts_lcl.h"

int TS_ASN1_INTEGER_print_bio(BIO *bio, const ASN1_INTEGER *num)
{
    BIGNUM *num_bn;
    int result = 0;
    char *hex;

    num_bn = ASN1_INTEGER_to_BN(num, NULL);
    if (num_bn == NULL)
        return -1;
    if ((hex = BN_bn2hex(num_bn))) {
        result = BIO_write(bio, "0x", 2) > 0;
        result = result && BIO_write(bio, hex, strlen(hex)) > 0;
        OPENSSL_free(hex);
    }
    BN_free(num_bn);

    return result;
}

int TS_OBJ_print_bio(BIO *bio, const ASN1_OBJECT *obj)
{
    char obj_txt[128];

    OBJ_obj2txt(obj_txt, sizeof(obj_txt), obj, 0);
    BIO_printf(bio, "%s\n", obj_txt);

    return 1;
}

int TS_ext_print_bio(BIO *bio, const STACK_OF(X509_EXTENSION) *extensions)
{
    int i, critical, n;
    X509_EXTENSION *ex;
    ASN1_OBJECT *obj;

    BIO_printf(bio, "Extensions:\n");
    n = X509v3_get_ext_count(extensions);
    for (i = 0; i < n; i++) {
        ex = X509v3_get_ext(extensions, i);
        obj = X509_EXTENSION_get_object(ex);
        if (i2a_ASN1_OBJECT(bio, obj) < 0)
            return 0;
        critical = X509_EXTENSION_get_critical(ex);
        BIO_printf(bio, ":%s\n", critical ? " critical" : "");
        if (!X509V3_EXT_print(bio, ex, 0, 4)) {
            BIO_printf(bio, "%4s", "");
            ASN1_STRING_print(bio, X509_EXTENSION_get_data(ex));
        }
        BIO_write(bio, "\n", 1);
    }

    return 1;
}

int TS_X509_ALGOR_print_bio(BIO *bio, const X509_ALGOR *alg)
{
    int i = OBJ_obj2nid(alg->algorithm);
    return BIO_printf(bio, "Hash Algorithm: %s\n",
                      (i == NID_undef) ? "UNKNOWN" : OBJ_nid2ln(i));
}

int TS_MSG_IMPRINT_print_bio(BIO *bio, TS_MSG_IMPRINT *a)
{
    ASN1_OCTET_STRING *msg;

    TS_X509_ALGOR_print_bio(bio, a->hash_algo);

    BIO_printf(bio, "Message data:\n");
    msg = a->hashed_msg;
    BIO_dump_indent(bio, (const char *)ASN1_STRING_get0_data(msg),
                    ASN1_STRING_length(msg), 4);

    return 1;
}
