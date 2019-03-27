/*
 * Copyright 2017-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * Certificate table information. NB: table entries must match SSL_PKEY indices
 */
static const SSL_CERT_LOOKUP ssl_cert_info [] = {
    {EVP_PKEY_RSA, SSL_aRSA}, /* SSL_PKEY_RSA */
    {EVP_PKEY_RSA_PSS, SSL_aRSA}, /* SSL_PKEY_RSA_PSS_SIGN */
    {EVP_PKEY_DSA, SSL_aDSS}, /* SSL_PKEY_DSA_SIGN */
    {EVP_PKEY_EC, SSL_aECDSA}, /* SSL_PKEY_ECC */
    {NID_id_GostR3410_2001, SSL_aGOST01}, /* SSL_PKEY_GOST01 */
    {NID_id_GostR3410_2012_256, SSL_aGOST12}, /* SSL_PKEY_GOST12_256 */
    {NID_id_GostR3410_2012_512, SSL_aGOST12}, /* SSL_PKEY_GOST12_512 */
    {EVP_PKEY_ED25519, SSL_aECDSA}, /* SSL_PKEY_ED25519 */
    {EVP_PKEY_ED448, SSL_aECDSA} /* SSL_PKEY_ED448 */
};
