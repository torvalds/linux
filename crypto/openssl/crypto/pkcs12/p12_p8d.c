/*
 * Copyright 2001-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/pkcs12.h>

PKCS8_PRIV_KEY_INFO *PKCS8_decrypt(const X509_SIG *p8, const char *pass,
                                   int passlen)
{
    const X509_ALGOR *dalg;
    const ASN1_OCTET_STRING *doct;
    X509_SIG_get0(p8, &dalg, &doct);
    return PKCS12_item_decrypt_d2i(dalg,
                                   ASN1_ITEM_rptr(PKCS8_PRIV_KEY_INFO), pass,
                                   passlen, doct, 1);
}
