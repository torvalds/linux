/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
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
#include <openssl/rsa.h>
#include <openssl/dsa.h>

#ifndef OPENSSL_NO_STDIO
int X509_REQ_print_fp(FILE *fp, X509_REQ *x)
{
    BIO *b;
    int ret;

    if ((b = BIO_new(BIO_s_file())) == NULL) {
        X509err(X509_F_X509_REQ_PRINT_FP, ERR_R_BUF_LIB);
        return 0;
    }
    BIO_set_fp(b, fp, BIO_NOCLOSE);
    ret = X509_REQ_print(b, x);
    BIO_free(b);
    return ret;
}
#endif

int X509_REQ_print_ex(BIO *bp, X509_REQ *x, unsigned long nmflags,
                      unsigned long cflag)
{
    long l;
    int i;
    EVP_PKEY *pkey;
    STACK_OF(X509_EXTENSION) *exts;
    char mlch = ' ';
    int nmindent = 0;

    if ((nmflags & XN_FLAG_SEP_MASK) == XN_FLAG_SEP_MULTILINE) {
        mlch = '\n';
        nmindent = 12;
    }

    if (nmflags == X509_FLAG_COMPAT)
        nmindent = 16;

    if (!(cflag & X509_FLAG_NO_HEADER)) {
        if (BIO_write(bp, "Certificate Request:\n", 21) <= 0)
            goto err;
        if (BIO_write(bp, "    Data:\n", 10) <= 0)
            goto err;
    }
    if (!(cflag & X509_FLAG_NO_VERSION)) {
        l = X509_REQ_get_version(x);
        if (l >= 0 && l <= 2) {
            if (BIO_printf(bp, "%8sVersion: %ld (0x%lx)\n", "", l + 1, (unsigned long)l) <= 0)
                goto err;
        } else {
            if (BIO_printf(bp, "%8sVersion: Unknown (%ld)\n", "", l) <= 0)
                goto err;
        }
    }
    if (!(cflag & X509_FLAG_NO_SUBJECT)) {
        if (BIO_printf(bp, "        Subject:%c", mlch) <= 0)
            goto err;
        if (X509_NAME_print_ex(bp, X509_REQ_get_subject_name(x),
            nmindent, nmflags) < 0)
            goto err;
        if (BIO_write(bp, "\n", 1) <= 0)
            goto err;
    }
    if (!(cflag & X509_FLAG_NO_PUBKEY)) {
        X509_PUBKEY *xpkey;
        ASN1_OBJECT *koid;
        if (BIO_write(bp, "        Subject Public Key Info:\n", 33) <= 0)
            goto err;
        if (BIO_printf(bp, "%12sPublic Key Algorithm: ", "") <= 0)
            goto err;
        xpkey = X509_REQ_get_X509_PUBKEY(x);
        X509_PUBKEY_get0_param(&koid, NULL, NULL, NULL, xpkey);
        if (i2a_ASN1_OBJECT(bp, koid) <= 0)
            goto err;
        if (BIO_puts(bp, "\n") <= 0)
            goto err;

        pkey = X509_REQ_get0_pubkey(x);
        if (pkey == NULL) {
            if (BIO_printf(bp, "%12sUnable to load Public Key\n", "") <= 0)
                goto err;
            ERR_print_errors(bp);
        } else {
            if (EVP_PKEY_print_public(bp, pkey, 16, NULL) <= 0)
                goto err;
        }
    }

    if (!(cflag & X509_FLAG_NO_ATTRIBUTES)) {
        /* may not be */
        if (BIO_printf(bp, "%8sAttributes:\n", "") <= 0)
            goto err;

        if (X509_REQ_get_attr_count(x) == 0) {
            if (BIO_printf(bp, "%12sa0:00\n", "") <= 0)
                goto err;
        } else {
            for (i = 0; i < X509_REQ_get_attr_count(x); i++) {
                ASN1_TYPE *at;
                X509_ATTRIBUTE *a;
                ASN1_BIT_STRING *bs = NULL;
                ASN1_OBJECT *aobj;
                int j, type = 0, count = 1, ii = 0;

                a = X509_REQ_get_attr(x, i);
                aobj = X509_ATTRIBUTE_get0_object(a);
                if (X509_REQ_extension_nid(OBJ_obj2nid(aobj)))
                    continue;
                if (BIO_printf(bp, "%12s", "") <= 0)
                    goto err;
                if ((j = i2a_ASN1_OBJECT(bp, aobj)) > 0) {
                    ii = 0;
                    count = X509_ATTRIBUTE_count(a);
 get_next:
                    at = X509_ATTRIBUTE_get0_type(a, ii);
                    type = at->type;
                    bs = at->value.asn1_string;
                }
                for (j = 25 - j; j > 0; j--)
                    if (BIO_write(bp, " ", 1) != 1)
                        goto err;
                if (BIO_puts(bp, ":") <= 0)
                    goto err;
                switch (type) {
                case V_ASN1_PRINTABLESTRING:
                case V_ASN1_T61STRING:
                case V_ASN1_NUMERICSTRING:
                case V_ASN1_UTF8STRING:
                case V_ASN1_IA5STRING:
                    if (BIO_write(bp, (char *)bs->data, bs->length)
                            != bs->length)
                        goto err;
                    if (BIO_puts(bp, "\n") <= 0)
                        goto err;
                    break;
                default:
                    if (BIO_puts(bp, "unable to print attribute\n") <= 0)
                        goto err;
                    break;
                }
                if (++ii < count)
                    goto get_next;
            }
        }
    }
    if (!(cflag & X509_FLAG_NO_EXTENSIONS)) {
        exts = X509_REQ_get_extensions(x);
        if (exts) {
            if (BIO_printf(bp, "%8sRequested Extensions:\n", "") <= 0)
                goto err;
            for (i = 0; i < sk_X509_EXTENSION_num(exts); i++) {
                ASN1_OBJECT *obj;
                X509_EXTENSION *ex;
                int critical;
                ex = sk_X509_EXTENSION_value(exts, i);
                if (BIO_printf(bp, "%12s", "") <= 0)
                    goto err;
                obj = X509_EXTENSION_get_object(ex);
                if (i2a_ASN1_OBJECT(bp, obj) <= 0)
                    goto err;
                critical = X509_EXTENSION_get_critical(ex);
                if (BIO_printf(bp, ": %s\n", critical ? "critical" : "") <= 0)
                    goto err;
                if (!X509V3_EXT_print(bp, ex, cflag, 16)) {
                    if (BIO_printf(bp, "%16s", "") <= 0
                        || ASN1_STRING_print(bp,
                                             X509_EXTENSION_get_data(ex)) <= 0)
                        goto err;
                }
                if (BIO_write(bp, "\n", 1) <= 0)
                    goto err;
            }
            sk_X509_EXTENSION_pop_free(exts, X509_EXTENSION_free);
        }
    }

    if (!(cflag & X509_FLAG_NO_SIGDUMP)) {
        const X509_ALGOR *sig_alg;
        const ASN1_BIT_STRING *sig;
        X509_REQ_get0_signature(x, &sig, &sig_alg);
        if (!X509_signature_print(bp, sig_alg, sig))
            goto err;
    }

    return 1;
 err:
    X509err(X509_F_X509_REQ_PRINT_EX, ERR_R_BUF_LIB);
    return 0;
}

int X509_REQ_print(BIO *bp, X509_REQ *x)
{
    return X509_REQ_print_ex(bp, x, XN_FLAG_COMPAT, X509_FLAG_COMPAT);
}
