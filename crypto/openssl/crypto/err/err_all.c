/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/err_int.h"
#include <openssl/asn1err.h>
#include <openssl/bnerr.h>
#include <openssl/ecerr.h>
#include <openssl/buffererr.h>
#include <openssl/bioerr.h>
#include <openssl/comperr.h>
#include <openssl/rsaerr.h>
#include <openssl/dherr.h>
#include <openssl/dsaerr.h>
#include <openssl/evperr.h>
#include <openssl/objectserr.h>
#include <openssl/pemerr.h>
#include <openssl/pkcs7err.h>
#include <openssl/x509err.h>
#include <openssl/x509v3err.h>
#include <openssl/conferr.h>
#include <openssl/pkcs12err.h>
#include <openssl/randerr.h>
#include "internal/dso.h"
#include <openssl/engineerr.h>
#include <openssl/uierr.h>
#include <openssl/ocsperr.h>
#include <openssl/err.h>
#include <openssl/tserr.h>
#include <openssl/cmserr.h>
#include <openssl/cterr.h>
#include <openssl/asyncerr.h>
#include <openssl/kdferr.h>
#include <openssl/storeerr.h>

int err_load_crypto_strings_int(void)
{
    if (
#ifndef OPENSSL_NO_ERR
        ERR_load_ERR_strings() == 0 ||    /* include error strings for SYSerr */
        ERR_load_BN_strings() == 0 ||
# ifndef OPENSSL_NO_RSA
        ERR_load_RSA_strings() == 0 ||
# endif
# ifndef OPENSSL_NO_DH
        ERR_load_DH_strings() == 0 ||
# endif
        ERR_load_EVP_strings() == 0 ||
        ERR_load_BUF_strings() == 0 ||
        ERR_load_OBJ_strings() == 0 ||
        ERR_load_PEM_strings() == 0 ||
# ifndef OPENSSL_NO_DSA
        ERR_load_DSA_strings() == 0 ||
# endif
        ERR_load_X509_strings() == 0 ||
        ERR_load_ASN1_strings() == 0 ||
        ERR_load_CONF_strings() == 0 ||
        ERR_load_CRYPTO_strings() == 0 ||
# ifndef OPENSSL_NO_COMP
        ERR_load_COMP_strings() == 0 ||
# endif
# ifndef OPENSSL_NO_EC
        ERR_load_EC_strings() == 0 ||
# endif
        /* skip ERR_load_SSL_strings() because it is not in this library */
        ERR_load_BIO_strings() == 0 ||
        ERR_load_PKCS7_strings() == 0 ||
        ERR_load_X509V3_strings() == 0 ||
        ERR_load_PKCS12_strings() == 0 ||
        ERR_load_RAND_strings() == 0 ||
        ERR_load_DSO_strings() == 0 ||
# ifndef OPENSSL_NO_TS
        ERR_load_TS_strings() == 0 ||
# endif
# ifndef OPENSSL_NO_ENGINE
        ERR_load_ENGINE_strings() == 0 ||
# endif
# ifndef OPENSSL_NO_OCSP
        ERR_load_OCSP_strings() == 0 ||
# endif
        ERR_load_UI_strings() == 0 ||
# ifndef OPENSSL_NO_CMS
        ERR_load_CMS_strings() == 0 ||
# endif
# ifndef OPENSSL_NO_CT
        ERR_load_CT_strings() == 0 ||
# endif
        ERR_load_ASYNC_strings() == 0 ||
#endif
        ERR_load_KDF_strings() == 0 ||
        ERR_load_OSSL_STORE_strings() == 0)
        return 0;

    return 1;
}
