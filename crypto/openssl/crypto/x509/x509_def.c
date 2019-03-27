/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/crypto.h>
#include <openssl/x509.h>

const char *X509_get_default_private_dir(void)
{
    return X509_PRIVATE_DIR;
}

const char *X509_get_default_cert_area(void)
{
    return X509_CERT_AREA;
}

const char *X509_get_default_cert_dir(void)
{
    return X509_CERT_DIR;
}

const char *X509_get_default_cert_file(void)
{
    return X509_CERT_FILE;
}

const char *X509_get_default_cert_dir_env(void)
{
    return X509_CERT_DIR_EVP;
}

const char *X509_get_default_cert_file_env(void)
{
    return X509_CERT_FILE_EVP;
}
