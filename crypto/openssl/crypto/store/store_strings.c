/*
 * Copyright 2016-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/store.h>

static char *type_strings[] = {
    "Name",                      /* OSSL_STORE_INFO_NAME */
    "Parameters",                /* OSSL_STORE_INFO_PARAMS */
    "Pkey",                      /* OSSL_STORE_INFO_PKEY */
    "Certificate",               /* OSSL_STORE_INFO_CERT */
    "CRL"                        /* OSSL_STORE_INFO_CRL */
};

const char *OSSL_STORE_INFO_type_string(int type)
{
    int types = sizeof(type_strings) / sizeof(type_strings[0]);

    if (type < 1 || type > types)
        return NULL;

    return type_strings[type - 1];
}
