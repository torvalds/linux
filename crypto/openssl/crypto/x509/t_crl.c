/*
 * Copyright 1999-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/buffer.h>
#include <openssl/bn.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#ifndef OPENSSL_NO_STDIO
int X509_CRL_print_fp(FILE *fp, X509_CRL *x)
{
    BIO *b;
    int ret;

    if ((b = BIO_new(BIO_s_file())) == NULL) {
        X509err(X509_F_X509_CRL_PRINT_FP, ERR_R_BUF_LIB);
        return 0;
    }
    BIO_set_fp(b, fp, BIO_NOCLOSE);
    ret = X509_CRL_print(b, x);
    BIO_free(b);
    return ret;
}
#endif

int X509_CRL_print(BIO *out, X509_CRL *x)
{
  return X509_CRL_print_ex(out, x, XN_FLAG_COMPAT);
}

int X509_CRL_print_ex(BIO *out, X509_CRL *x, unsigned long nmflag)
{
    STACK_OF(X509_REVOKED) *rev;
    X509_REVOKED *r;
    const X509_ALGOR *sig_alg;
    const ASN1_BIT_STRING *sig;
    long l;
    int i;

    BIO_printf(out, "Certificate Revocation List (CRL):\n");
    l = X509_CRL_get_version(x);
    if (l >= 0 && l <= 1)
        BIO_printf(out, "%8sVersion %ld (0x%lx)\n", "", l + 1, (unsigned long)l);
    else
        BIO_printf(out, "%8sVersion unknown (%ld)\n", "", l);
    X509_CRL_get0_signature(x, &sig, &sig_alg);
    BIO_puts(out, "    ");
    X509_signature_print(out, sig_alg, NULL);
    BIO_printf(out, "%8sIssuer: ", "");
    X509_NAME_print_ex(out, X509_CRL_get_issuer(x), 0, nmflag);
    BIO_puts(out, "\n");
    BIO_printf(out, "%8sLast Update: ", "");
    ASN1_TIME_print(out, X509_CRL_get0_lastUpdate(x));
    BIO_printf(out, "\n%8sNext Update: ", "");
    if (X509_CRL_get0_nextUpdate(x))
        ASN1_TIME_print(out, X509_CRL_get0_nextUpdate(x));
    else
        BIO_printf(out, "NONE");
    BIO_printf(out, "\n");

    X509V3_extensions_print(out, "CRL extensions",
                            X509_CRL_get0_extensions(x), 0, 8);

    rev = X509_CRL_get_REVOKED(x);

    if (sk_X509_REVOKED_num(rev) > 0)
        BIO_printf(out, "Revoked Certificates:\n");
    else
        BIO_printf(out, "No Revoked Certificates.\n");

    for (i = 0; i < sk_X509_REVOKED_num(rev); i++) {
        r = sk_X509_REVOKED_value(rev, i);
        BIO_printf(out, "    Serial Number: ");
        i2a_ASN1_INTEGER(out, X509_REVOKED_get0_serialNumber(r));
        BIO_printf(out, "\n        Revocation Date: ");
        ASN1_TIME_print(out, X509_REVOKED_get0_revocationDate(r));
        BIO_printf(out, "\n");
        X509V3_extensions_print(out, "CRL entry extensions",
                                X509_REVOKED_get0_extensions(r), 0, 8);
    }
    X509_signature_print(out, sig_alg, sig);

    return 1;

}
